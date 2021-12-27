#include "accessmediator.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>

#include <fcntl.h>
#include <unistd.h>

#define CONTROL_DEVICE "/dev/v4l2loopback"

enum v4l2_loopback_hint_type {
    HINT_UNKNOWN = 0,
    HINT_OPEN,
    HINT_CLOSE
};

struct v4l2_loopback_hint {
    enum v4l2_loopback_hint_type type;
    uid_t uid;
    pid_t pid;
    int node;
};

AccessMediator::AccessMediator(QObject *parent) :
    QObject(parent),
    m_notifyThread(new QThread(this)),
    m_running(true)
{
    this->m_notifyFd = open(CONTROL_DEVICE, O_RDONLY);
    if (this->m_notifyFd < 0) {
        qFatal("Failed to open control device: %s", strerror(errno));
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

        static const int BUFSIZE = sizeof(struct v4l2_loopback_hint);
        char buffer[sizeof(BUFSIZE)];

        const int length = read(this->m_notifyFd, buffer, BUFSIZE);
        if (length < 0) {
            qWarning("Failed to read from notification fd: %s", strerror(errno));
            continue;
        }

        struct v4l2_loopback_hint* hint = (struct v4l2_loopback_hint*) &buffer;
        if (hint->type == HINT_UNKNOWN)
            continue;

        if (hint->pid == getpid())
            continue;

        const QString deviceName = QStringLiteral("/dev/video%1").arg(hint->node);

        switch (hint->type) {
        case HINT_OPEN:
            qDebug() << "Device accessed by:" << hint->pid;
            qInfo("Access allowed for %s", deviceName.toUtf8().data());
            emit accessAllowed(deviceName);
            break;
        case HINT_CLOSE:
            qInfo("Device %s closed by %d", deviceName.toUtf8().data(), hint->pid);
            emit deviceClosed(deviceName);
            break;
        default:
            qDebug("Unknown hint type received: %d", hint->type);
            break;
        }
    }

    qInfo("Notification loop stopped!");
}

void AccessMediator::registerDevice(const QString path)
{
    this->m_devices.insert(path.toStdString());
    qInfo("Registered watcher for node %s", path.toUtf8().data());
}

void AccessMediator::unregisterDevice(const QString path)
{
    const std::string stdPath = path.toStdString();
    if (this->m_devices.find(stdPath) == this->m_devices.end()) {
        qWarning("Device %s not registered, skipping...", path.toUtf8().data());
        return;
    }

    this->m_devices.erase(stdPath);
}
