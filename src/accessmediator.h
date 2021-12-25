#ifndef ACCESSMEDIATOR_H
#define ACCESSMEDIATOR_H

#include <QObject>
#include <QTimer>
#include <QThread>

#include <map>
#include <set>
#include <vector>

struct TrackingInfo {
    ~TrackingInfo() {
        qDebug("Tracking info removed with %d open accesses",
               runningPids.size());
    }
    std::vector<quint64> runningPids; // TODO: turn this into a set
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

private slots:
    void startDecisionMaking();
    void accessDecision();

private:
    void runNotificationLoop();
    std::vector<quint64> findUsingPids(const QString& device);

    bool m_running;
    QThread* m_notifyThread;
    int m_notifyFd;
    std::set<int> m_watchers;
    std::map<const QString, TrackingInfo*> m_devices;
    QTimer m_delayedDecision;

signals:
    void permitted(const quint64 pid);
    void denied(const quint64 pid);
    void accessAllowed(const QString path);
    void deviceClosed(const QString path);
};

#endif // ACCESSMEDIATOR_H
