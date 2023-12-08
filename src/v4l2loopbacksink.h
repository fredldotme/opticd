#ifndef V4L2LOOPBACKSINK_H
#define V4L2LOOPBACKSINK_H

#include <QObject>

class V4L2LoopbackSink : public QObject
{
    Q_OBJECT
public:
    explicit V4L2LoopbackSink(size_t width = 0,
                              size_t height = 0,
                              QString description = QStringLiteral("null"),
                              QObject *parent = nullptr);
    ~V4L2LoopbackSink();

    void pushCapture(QByteArray capture);
    void run();

private:
    void addLoopbackDevice();
    void openLoopbackDevice();
    void deleteLoopbackDevice();
    void feedDummyFrame();

    QString m_path;
    QString m_description;
    int m_width = 0;
    int m_height = 0;
    int m_deviceNumber = 0;
    int m_sinkFd = -1;
    int m_vidsendsiz = 0;

signals:
    void deviceCreated(const QString path);
    void deviceRemoved(const QString path);

};

#endif // V4L2LOOPBACKSINK_H
