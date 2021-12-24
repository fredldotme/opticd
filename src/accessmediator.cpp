#include "accessmediator.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>

#include <dirent.h>
#include <sys/inotify.h>
#include <unistd.h>

#define EVENT_SIZE  (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

AccessMediator::AccessMediator(QObject *parent) :
    QObject(parent),
    m_notifyThread(new QThread(this)),
    m_running(true)
{
    this->m_notifyFd = inotify_init();
    if (this->m_notifyFd < 0) {
        qFatal("Failed to initialize inotify watcher: %s", strerror(errno));
        exit(2);
        return;
    }


    int watchFd = inotify_add_watch(this->m_notifyFd,
                                    "/dev",
                                    IN_OPEN | IN_CLOSE);
    if (watchFd < 0) {
        qFatal("Failed to create watchFd: %s", strerror(errno));
        exit(2);
        return;
    }

    this->m_watchers.insert(watchFd);

    QObject::connect(this->m_notifyThread, &QThread::started,
                     this, &AccessMediator::runNotificationLoop, Qt::DirectConnection);
    this->m_notifyThread->start();
}

AccessMediator::~AccessMediator()
{
    this->m_running = false;
    this->m_notifyThread->terminate();
    this->m_notifyThread->wait(1000);
    close(this->m_notifyFd);
}

static inline bool hasSamePids(
        const std::vector<quint64>& first,
        const std::vector<quint64>& second)
{
    for (const quint64& pid1 : first) {
        for (const quint64 pid2 : second)
            if (pid1 != pid2)
                return false;
    }
    return true;
}

static inline bool hasNewPids(
        const std::vector<quint64>& oldPids,
        const std::vector<quint64>& newPids)
{
    for (const quint64& pid : newPids) {
        if (std::find(oldPids.begin(), oldPids.end(), pid) == oldPids.end())
            return true;
    }
    return false;
}

void AccessMediator::runNotificationLoop()
{
    fd_set set;
    struct timeval tv;

    FD_ZERO(&set);

    while (this->m_running)
    {
        FD_SET(this->m_notifyFd, &set);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int n = select(FD_SETSIZE, &set, NULL, NULL, &tv);

        // It's time to go
        if (!this->m_running)
            break;

        if (!n || n == -1)
            continue;

        char buffer[EVENT_BUF_LEN];
        const int length = read(this->m_notifyFd, buffer, EVENT_BUF_LEN);
        if (length < 0) {
            qWarning("Failed to read from notification fd: %s", strerror(errno));
            continue;
        }

        int i = 0;
        while (i < length) {
            struct inotify_event* event = (struct inotify_event*) &buffer[i];

            if (event->len <= 0) {
                i += (EVENT_SIZE+event->len);
                continue;
            }

            const QString deviceName = QStringLiteral("/dev/%1").arg(event->name);

            // Skip devices we don't manage
            if (this->m_devices.find(deviceName) == this->m_devices.end()) {
                i += (EVENT_SIZE+event->len);
                continue;
            }

            // Skip those events that would cancel each other out
            // Don't know if those exist, but let's be safe
            if (event->mask & IN_OPEN && event->mask & IN_CLOSE) {
                i += (EVENT_SIZE+event->len);
                continue;
            }

            const std::vector<quint64> openPids = this->m_devices[deviceName]->runningPids;
            const std::vector<quint64> pids = findUsingPids(deviceName);

            // Do the pid matching now
            if (event->mask & IN_OPEN) {
                qDebug() << "Device accessed by:" << pids;
                this->m_devices[deviceName]->runningPids = pids;
                if (pids.size() >= 1) {
                    qInfo("Access allowed for %s", deviceName.toUtf8().data());
                    emit accessAllowed(deviceName);
                }
            } else if (event->mask & IN_CLOSE) {
                this->m_devices[deviceName]->runningPids = pids;
                if (pids.size() <= 0) {
                    qInfo("Device %s closed with open count %d", deviceName.toUtf8().data(), pids.size());
                    emit deviceClosed(deviceName);
                }
            }

            i += (EVENT_SIZE+event->len);
        }
    }

    qInfo("Notification loop stopped!");
}

static int separatorsInPath(const QString& path)
{
    int ret = 0;
    for (const QChar& c : path) {
        if (c == '/') ret += 1;
    }
    return ret;
}

std::vector<quint64> AccessMediator::findUsingPids(const QString &device)
{    
    std::vector<quint64> pids;

    DIR *proc = opendir("/proc");
    struct dirent* procContents;
    if (!proc) {
        qFatal("Couldn't open /proc: %s", strerror(errno));
    }

    while ((procContents = readdir(proc)) != NULL) {
        // Skip ourselves
        const quint64 thatpid = atol(procContents->d_name);
        if (thatpid == 0)
            continue;

        if (thatpid == getpid())
            continue;

        QString fdDir = QStringLiteral("/proc/%1/fd").arg(QString::fromUtf8(procContents->d_name));
        const QByteArray& fdDirUtf8 = fdDir.toUtf8();

        if (access(fdDirUtf8.data(), F_OK) != 0)
            continue;

        DIR* procFd = opendir(fdDirUtf8.data());
        struct dirent* fdContents;
        if (!procFd) {
            continue;
        }

        while ((fdContents = readdir(procFd)) != NULL) {
            QString fdLink = QStringLiteral("%1/%2").arg(fdDir, QString::fromUtf8(fdContents->d_name));
            const QByteArray& fdLinkUtf8 = fdLink.toUtf8();

            char fdLinkTarget[PATH_MAX];

            ssize_t len = readlink(fdLinkUtf8.data(), fdLinkTarget, PATH_MAX);
            fdLinkTarget[len] = '\0';

            if (QString::fromUtf8(fdLinkTarget) != device)
                continue;

            qDebug() << "  pid: " << procContents->d_name << "target: " << fdLinkTarget;

            pids.push_back(atoi(procContents->d_name));
        }

        closedir(procFd);
    }

    closedir(proc);

    return pids;
}

void AccessMediator::registerDevice(const QString path)
{
    TrackingInfo* info = new TrackingInfo;
    this->m_devices.insert({path, info});
    qInfo("Registered watcher for node %s", path.toUtf8().data());
}

void AccessMediator::unregisterDevice(const QString path)
{
    if (this->m_devices.find(path) == this->m_devices.end()) {
        qWarning("Device %s not registered, skipping...", path.toUtf8().data());
        return;
    }

    this->m_devices.erase(path);
}
