#include "accessmediator.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define CONTROLDEVICE "/dev/v4l2loopback"

enum MessageType {
    OPEN_HINT = 0,
    PERMISSION_HINT,
    CLOSE_HINT
};

enum PermissionState {
    PERMISSION_UNKNOWN = 0,
    PERMISSION_GRANTED,
    PERMISSION_DENIED
};

struct ControlMessage {
    MessageType type;
};

struct PermissionHeader {
    pid_t pid;
    uid_t uid;
};

struct PermissionResponse {
    PermissionHeader header;
    PermissionState state;
};

AccessMediator::AccessMediator(QObject *parent) :
    QObject(parent), m_loopThread(new QThread), m_running(true)
{
    QObject::connect(this->m_loopThread, &QThread::started,
                     this, &AccessMediator::runLoop, Qt::DirectConnection);
    this->m_loopThread->start();
}

AccessMediator::~AccessMediator()
{
    this->m_running = false;
    this->m_loopThread->terminate();
    this->m_loopThread->wait(3000);
}

void AccessMediator::runLoop()
{
    int fd;
    fd_set set;
    struct timeval tv;

    fd = open(CONTROLDEVICE, O_RDWR);
    if (fd < 0) {
        qWarning() << "Failed to open control device";
        return;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    FD_ZERO(&set);
    FD_SET(fd, &set);

    while (this->m_running)
    {
        int n = select(fd+1, NULL, &set, NULL, &tv);

        if (!n || n == -1 || !m_running)
            break;

        ControlMessage message;
        const ssize_t size = read(fd, (void*) &message, sizeof(struct ControlMessage));
        if (size <= 0) {
            qWarning() << "Received invalid control message from kernel";
            continue;
        }

        if (size != sizeof(struct ControlMessage)) {
            qWarning() << "Received a control message of size" << size;
            continue;
        }

        switch (message.type) {
        case OPEN_HINT:
            break;
        case PERMISSION_HINT:
        {
            PermissionHeader request;
            if (!read(fd, (void*) &request, sizeof(struct PermissionHeader))) {
                qWarning() << "Failed to read permission request";
                break;
            }

            // For now: immediately allow access
            PermissionResponse response;
            response.header = request;
            response.state = PermissionState::PERMISSION_GRANTED;
            write(fd, (const void*) &response, sizeof(struct PermissionResponse));
        }
            break;
        case CLOSE_HINT:
            break;
        default:
            qDebug() << "Unsupported control message" << message.type << "received";
            break;
        }
    }
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
