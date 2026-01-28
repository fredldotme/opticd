#include "accessmediator.h"

#include <QDebug>
#include <QDBusMetaType>
#include <QDBusConnection>

#include <fcntl.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>

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

static void enableProcessEventListener(int nl_sock, bool enable)
{
    int rc;
    struct nl_data {
        enum proc_cn_mcast_op cn_mcast;
        struct cn_msg cn_msg;
    };
    struct nlcn_msg {
        struct nlmsghdr nl_hdr;
        struct nl_data nl_data;
    };

    struct nlcn_msg nlcn_msg;

    memset(&nlcn_msg, 0, sizeof(nlcn_msg));
    nlcn_msg.nl_hdr.nlmsg_len = sizeof(nlcn_msg);
    nlcn_msg.nl_hdr.nlmsg_pid = getpid();
    nlcn_msg.nl_hdr.nlmsg_type = NLMSG_DONE;

    nlcn_msg.nl_data.cn_msg.id.idx = CN_IDX_PROC;
    nlcn_msg.nl_data.cn_msg.id.val = CN_VAL_PROC;
    nlcn_msg.nl_data.cn_msg.len = sizeof(enum proc_cn_mcast_op);

    nlcn_msg.nl_data.cn_mcast = enable ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;

    rc = send(nl_sock, &nlcn_msg, sizeof(nlcn_msg), 0);
    if (rc < 0) {
        qFatal("Failed to %s process event listener: %s", enable ? "enable" : "disable", strerror(errno));
        return;
    }

    return;
}

AccessMediator::AccessMediator(QObject *parent) :
    QObject(parent),
    m_notifyThread(new QThread(this)),
    m_netlinkThread(new QThread(this)),
    m_running(true)
{
    this->m_notifyFd = open(CONTROL_DEVICE, O_RDONLY);
    if (this->m_notifyFd < 0) {
        qFatal("Failed to open control device: %s", strerror(errno));
        exit(2);
        return;
    }

    this->m_netlinkFd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (this->m_netlinkFd < 0) {
        qWarning("Failed to open process event netlink socket: %s", strerror(errno));
    } else {
        struct sockaddr_nl procEventNl;
        procEventNl.nl_family = AF_NETLINK;
        procEventNl.nl_groups = CN_IDX_PROC;
        procEventNl.nl_pid = getpid();

        int rc = bind(this->m_netlinkFd, (struct sockaddr *)&procEventNl, sizeof(procEventNl));
        if (rc < 0) {
            qWarning("Failed to bind process event netlink socket: %s", strerror(errno));
        } else {
            enableProcessEventListener(this->m_netlinkFd, true);
        }
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

    QObject::connect(this->m_netlinkThread, &QThread::started,
                     this, &AccessMediator::runProcessNotificationLoop, Qt::DirectConnection);
    this->m_netlinkThread->start();
}

AccessMediator::~AccessMediator()
{
    this->m_running = false;

    if (this->m_notifyFd >= 0)
        close(this->m_notifyFd);

    if (this->m_netlinkFd >= 0) {
        enableProcessEventListener(this->m_netlinkFd, false);
        close(this->m_netlinkFd);
    }

    this->m_notifyThread->terminate();
    this->m_netlinkThread->terminate();

    this->m_notifyThread->wait(1000);
    this->m_netlinkThread->wait(1000);
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

void AccessMediator::runProcessNotificationLoop()
{
    int rc;
    struct nl_data {
        struct proc_event proc_ev;
        struct cn_msg cn_msg;
    };
    struct nlcn_msg {
        struct nlmsghdr nl_hdr;
        struct nl_data nl_data;
    };

    struct nlcn_msg nlcn_msg;

    if (this->m_netlinkFd < 0)
        return;

    while (this->m_running) {
        rc = recv(this->m_netlinkFd, &nlcn_msg, sizeof(nlcn_msg), 0);
        if (rc == 0) {
            return;
        } else if (rc < 0) {
            if (errno == EINTR) continue;
            qWarning("netlink recv");
            return;
        }

        switch (nlcn_msg.nl_data.proc_ev.what) {
            case proc_cn_event::PROC_EVENT_EXIT:
            {
                const auto& pid = nlcn_msg.nl_data.proc_ev.event_data.exit.process_tgid;

                for (const auto& device : this->m_devices) {
                    std::map<int, int> &fdsPerPid = this->m_devices[device.first].fdsPerPid;
                    if (fdsPerPid.find(pid) == fdsPerPid.end())
                        continue;

                    fdsPerPid.erase(pid);
                    qInfo("Device %s closed due to exit of %d", device.first.c_str(), pid);
                    emit deviceClosed(QString::fromStdString(device.first));
                }
            }
                break;
            default:
                break;
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
            if (--fdsPerPid[hint->pid] <= 0) {
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
