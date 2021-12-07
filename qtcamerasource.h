#ifndef QTCAMERASOURCE_H
#define QTCAMERASOURCE_H

#include <QObject>
#include <QByteArray>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#include <QAbstractVideoSurface>
#include <QCamera>
#include <QCameraImageCapture>

class QtCameraSurface: public QAbstractVideoSurface
{
    Q_OBJECT

public:
    QtCameraSurface(QObject* parent = nullptr) : QAbstractVideoSurface(parent) {}

    QList<QVideoFrame::PixelFormat> supportedPixelFormats(QAbstractVideoBuffer::HandleType type) const
    {
        return QList<QVideoFrame::PixelFormat>() << QVideoFrame::Format_RGB24;
    }

    bool present(const QVideoFrame& frame)
    {
        if (!frame.isValid())
            return false;

        /*QVideoFrame cloneFrame(frame);
        cloneFrame.map(QAbstractVideoBuffer::ReadOnly);
        const QImage img(cloneFrame.bits(),
                         cloneFrame.width(),
                         cloneFrame.height(),
                         QVideoFrame::imageFormatFromPixelFormat(cloneFrame.pixelFormat()));
        {
            QMutexLocker locker(&this->m_captureMutex);
            this->m_lastCapture = img;
            qDebug("Captured %d bytes", (int)img.sizeInBytes());
        }
        cloneFrame.unmap();*/

        if(!isActive())
            return false;

        qDebug("Presenting");

        QVideoFrame f = frame;
        f.map(QAbstractVideoBuffer::ReadOnly);

        QImage image(f.bits(), f.width(), f.height(), f.bytesPerLine(),
                      QVideoFrame::imageFormatFromPixelFormat(f.pixelFormat()));

        f.unmap();

        this->m_lastCapture = image.copy(image.rect());
        return true;
    }

    QImage lastCapture() {
        return this->m_lastCapture;
    }

private:
    QImage m_lastCapture;
    QMutex m_captureMutex;
};

class QtCameraSource : public QObject
{
    Q_OBJECT

public:
    explicit QtCameraSource(QCamera::Position position = QCamera::Position::UnspecifiedPosition,
                            QObject *parent = nullptr);
    void start();
    void requestFrame();
    void stop();

    size_t width();
    size_t height();

private:
    QCamera* m_camera = nullptr;
    QtCameraSurface* m_surface = nullptr;

signals:
    void captured(QByteArray frame);

};

#endif // QTCAMERASOURCE_H
