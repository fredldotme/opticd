#include "accessmediator.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>

#include <sys/inotify.h>
#include <unistd.h>

#define EVENT_SIZE  (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

AccessMediator::AccessMediator(QObject *parent) :
    QObject(parent),
    m_notifyThread(new QThread),
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

            if (this->m_devices.find(deviceName) == this->m_devices.end()) {
                i += (EVENT_SIZE+event->len);
                continue;
            }

            if (event->mask & IN_OPEN) {
                qInfo("Access allowed for %s", deviceName.toUtf8().data());
                emit accessAllowed(deviceName);
            } else if (event->mask & IN_CLOSE) {
                qInfo("Device %s closed", deviceName.toUtf8().data());
                emit deviceClosed(deviceName);
            }

            i += (EVENT_SIZE+event->len);
        }
    }

    qInfo("Notification loop stopped!");
}

QVector<quint64> AccessMediator::findUsingPids(const QString &device)
{
    QVector<quint64> pids;

    QDirIterator dirIt(QStringLiteral("/proc"), QStringList() << "fd", QDir::Dirs, QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        const QString path = dirIt.next();

        // Skip ourselves
        if (path == QStringLiteral("/proc/%1/fd").arg(getpid()))
            continue;

        // Skip proc entries of other users
        if (!dirIt.fileInfo().isReadable())
            continue;

        // Get the pid
        QStringList pathSplit = path.split('/');
        if (pathSplit.length() < 2)
            continue;
        bool parseOk = false;
        quint64 pid = pathSplit[1].toULongLong(&parseOk);
        if (!parseOk)
            continue;

        // Find all open files for the process
        QDirIterator procIt(path, QDir::Files, QDirIterator::FollowSymlinks);
        while (procIt.hasNext()) {
            const QString file = procIt.next();

            if (file == device) {
                pids.push_back(pid);
            }
        }
    }

    return pids;
}

void AccessMediator::registerDevice(const QString path)
{
    this->m_devices.insert(path);
    qInfo("Registered watcher for node %s", path.toUtf8().data());
}

void AccessMediator::unregisterDevice(const QString path)
{
    // TODO
}
