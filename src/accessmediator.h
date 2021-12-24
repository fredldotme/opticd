#ifndef ACCESSMEDIATOR_H
#define ACCESSMEDIATOR_H

#include <QObject>
#include <QThread>
#include <QVector>

#include <set>

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
    std::set<QString> m_devices;

signals:
    void permitted(const quint64 pid);
    void denied(const quint64 pid);
    void accessAllowed(const QString path);
    void deviceClosed(const QString path);
};

#endif // ACCESSMEDIATOR_H
