#ifndef ACCESSMEDIATOR_H
#define ACCESSMEDIATOR_H

#include <QObject>
#include <QThread>
#include <QVector>

#include <map>
#include <set>

struct TrackingInfo {
    ~TrackingInfo() {
        qDebug("Tracking info removed with %d open accesses",
               openFds.load(std::memory_order_acquire));
    }
    std::atomic<int> openFds;
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
    QVector<quint64> findUsingPids(const QString& device);

    bool m_running;
    QThread* m_notifyThread;
    int m_notifyFd;
    std::set<int> m_watchers;
    std::map<const QString, TrackingInfo*> m_devices;

signals:
    void permitted(const quint64 pid);
    void denied(const quint64 pid);
    void accessAllowed(const QString path);
    void deviceClosed(const QString path);
};

#endif // ACCESSMEDIATOR_H
