#ifndef ACCESSMEDIATOR_H
#define ACCESSMEDIATOR_H

#include <QObject>
#include <QVector>

class AccessMediator : public QObject
{
    Q_OBJECT
public:
    explicit AccessMediator(QObject *parent = nullptr);

private:
    QVector<quint64> findUsingPids(const QString& device);

signals:
    void permitted(const quint64 pid);
    void denied(const quint64 pid);

};

#endif // ACCESSMEDIATOR_H
