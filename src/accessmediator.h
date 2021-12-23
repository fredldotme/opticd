#ifndef ACCESSMEDIATOR_H
#define ACCESSMEDIATOR_H

#include <QObject>
#include <QVector>
#include <QThread>

class AccessMediator : public QObject
{
    Q_OBJECT
public:
    explicit AccessMediator(QObject *parent = nullptr);
    ~AccessMediator();

private:
    void runLoop();
    QVector<quint64> findUsingPids(const QString& device);

    QThread* m_loopThread;
    bool m_running;

signals:
    void permitted(const quint64 pid);
    void denied(const quint64 pid);

};

#endif // ACCESSMEDIATOR_H
