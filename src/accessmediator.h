#ifndef ACCESSMEDIATOR_H
#define ACCESSMEDIATOR_H

#include <QObject>
#include <QTimer>
#include <QThread>

#include <map>
#include <set>
#include <vector>

#include <unistd.h>

struct TrackingInfo {
    std::map<pid_t, int> fdsPerPid;
};

class AccessMediator : public QObject
{
    Q_OBJECT
public:
    explicit AccessMediator(QObject *parent = nullptr);
    ~AccessMediator();

public slots:
    void registerDevice(const QString path);
    void unregisterDevice(const QString path);

private:
    void runNotificationLoop();

    bool m_running;
    QThread* m_notifyThread;
    int m_notifyFd;
    std::map<std::string, TrackingInfo> m_devices;

signals:
    void permitted(const quint64 pid);
    void denied(const quint64 pid);
    void accessAllowed(const QString path);
    void deviceClosed(const QString path);
};

#endif // ACCESSMEDIATOR_H
