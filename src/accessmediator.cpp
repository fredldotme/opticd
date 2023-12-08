#include "accessmediator.h"

#include <QDebug>
#include <QDBusMetaType>
#include <QDBusConnection>

#include <fcntl.h>

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

QDBusArgument &operator<<(QDBusArgument &argument, const Pids &msg)
{
    argument.beginArray(qMetaTypeId<int>());
    for (int i = 0; i < msg.pids.length(); ++i)
        argument << msg.pids[i];
    argument.endArray();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, Pids &msg)
{
    argument.beginArray();
    while (!argument.atEnd()) {
        int pid;
        argument >> pid;
        msg.pids.append(pid);
    }
    argument.endArray();
    return argument;
}

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

    qDBusRegisterMetaType<Pids>();
    qDebug() << QDBusConnection::sessionBus().connect("",
                                                      "/",
                                                      "com.lomiri.LomiriAppLaunch",
                                                      "ApplicationPaused",
                                                      this,
                                                      SLOT(appPaused(QString, Pids)));
    qDebug() << QDBusConnection::sessionBus().connect("",
                                                      "/",
                                                      "com.lomiri.LomiriAppLaunch",
                                                      "ApplicationResumed",
                                                      this,
                                                      SLOT(appResumed(QString, Pids)));

    QObject::connect(this->m_notifyThread, &QThread::started,
                     this, &AccessMediator::runNotificationLoop, Qt::DirectConnection);
    this->m_notifyThread->start();
}

AccessMediator::~AccessMediator()
{
    this->m_running = false;
    close(this->m_notifyFd);
    this->m_notifyThread->terminate();
    this->m_notifyThread->wait(1000);
}

void AccessMediator::appPaused(QString name, Pids pids)
{
    for (const auto& device : this->m_devices) {
        const TrackingInfo& tracking = this->m_devices[device.first];
        for (const int& pid : pids.pids) {
            if (tracking.fdsPerPid.find(pid) != tracking.fdsPerPid.end()) {
                emit deviceClosed(QString::fromStdString(device.first));
            }
        }
    }
}

void AccessMediator::appResumed(QString name, Pids pids)
{
    for (const auto& device : this->m_devices) {
        const TrackingInfo& tracking = this->m_devices[device.first];
        for (const int& pid : pids.pids) {
            if (tracking.fdsPerPid.find(pid) != tracking.fdsPerPid.end()) {
                emit accessAllowed(QString::fromStdString(device.first));
            }
        }
    }
}

void AccessMediator::runNotificationLoop()
{
    while (this->m_running)
    {
        static const int BUFSIZE = sizeof(struct v4l2_loopback_hint);
        char buffer[BUFSIZE];

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

        if (this->m_devices.find(deviceName.toStdString()) == this->m_devices.end())
            continue;

        const std::string stdDeviceName = deviceName.toStdString();
        std::map<int, int> &fdsPerPid = this->m_devices[stdDeviceName].fdsPerPid;

        switch (hint->type) {
        case HINT_OPEN:
            if (fdsPerPid.find(hint->pid) != fdsPerPid.end())
                ++fdsPerPid[hint->pid];
            else
                fdsPerPid[hint->pid] = 1;

            qDebug() << "Device accessed by:" << hint->pid;
            qInfo("Access allowed for %s", deviceName.toUtf8().data());
            emit accessAllowed(deviceName);

            break;
        case HINT_CLOSE:
            if (--fdsPerPid[hint->pid] == 0) {
                fdsPerPid.erase(hint->pid);
                qInfo("Device %s closed by %d", deviceName.toUtf8().data(), hint->pid);
                emit deviceClosed(deviceName);
            }
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
    TrackingInfo info;
    this->m_devices.insert({path.toStdString(), info});
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
