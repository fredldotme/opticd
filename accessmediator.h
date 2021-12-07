#ifndef ACCESSMEDIATOR_H
#define ACCESSMEDIATOR_H

#include <QObject>

class AccessMediator : public QObject
{
    Q_OBJECT
public:
    explicit AccessMediator(QObject *parent = nullptr);

signals:

};

#endif // ACCESSMEDIATOR_H
