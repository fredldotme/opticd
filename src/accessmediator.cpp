#include "accessmediator.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>

#include <dirent.h>
#include <fcntl.h>
#include <linux/fanotify.h>
#include <sys/fanotify.h>
#include <unistd.h>

static const int FANOTIFY_BUFFER_SIZE = 8192;
static const uint64_t EVENTMASK = (FAN_ACCESS | FAN_CLOSE_NOWRITE);

AccessMediator::AccessMediator(QObject *parent) :
    QObject(parent),
    m_notifyThread(new QThread(this)),
    m_running(true)
{
    this->m_notifyFd = fanotify_init(FAN_CLOEXEC, O_RDONLY | O_CLOEXEC | O_LARGEFILE);
    if (this->m_notifyFd < 0) {
        qFatal("Failed to initialize file access notification watcher: %s", strerror(errno));
        exit(2);
        return;
    }

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

static inline QString getNodeForPid(const int& fd)
{
    const QString pattern = QStringLiteral("/proc/self/fd/%1").arg(fd);
    char fdLinkTarget[PATH_MAX];
    ssize_t len = readlink(pattern.toUtf8().data(), fdLinkTarget, PATH_MAX);
    fdLinkTarget[len] = '\0';
    return QString::fromUtf8(fdLinkTarget);
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

        char buffer[FANOTIFY_BUFFER_SIZE];
        int length = read(this->m_notifyFd, buffer, FANOTIFY_BUFFER_SIZE);
        if (length < 0) {
            qWarning("Failed to read from notification fd: %s", strerror(errno));
            continue;
        }

        struct fanotify_event_metadata* event = (struct fanotify_event_metadata*) &buffer;

        while (FAN_EVENT_OK(event, length)) {
            // Skip those events that would cancel each other out
            // Don't know if those exist, but let's be safe
            if (event->mask & FAN_ACCESS && event->mask & FAN_CLOSE_NOWRITE) {
                event = FAN_EVENT_NEXT(event, length);
                continue;
            }

            // Start the decision making
            if (event->mask & FAN_ACCESS) {
                // Do the pid matching now
                qDebug() << "Device accessed by:" << event->pid;
                const QString deviceName = getNodeForPid(event->fd);
                qInfo("Access allowed for %s", deviceName.toUtf8().data());
                emit accessAllowed(deviceName);
            } else {
                const QString deviceName = getNodeForPid(event->fd);
                qInfo("Device %s closed", deviceName.toUtf8().data());
                emit deviceClosed(deviceName);
            }
            close(event->fd);
            event = FAN_EVENT_NEXT(event, length);
        }
    }

    qInfo("Notification loop stopped!");
}

void AccessMediator::registerDevice(const QString path)
{
    if (fanotify_mark(this->m_notifyFd, FAN_MARK_ADD, EVENTMASK, AT_FDCWD, path.toUtf8().data()) < 0) {
        qFatal("Failed to set notify mark on %s", path.toUtf8().data());
        exit(2);
        return;
    }

    this->m_devices.insert(path.toStdString());
    qInfo("Registered watcher for node %s", path.toUtf8().data());
}

void AccessMediator::unregisterDevice(const QString path)
{
    auto it = this->m_devices.find(path.toStdString());
    if (it != this->m_devices.end())
        this->m_devices.erase(it);
}
