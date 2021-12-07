#include "qtcamerasource.h"

QtCameraSource::QtCameraSource(QCamera::Position position, QObject *parent) : QObject(parent)
{
    this->m_camera = new QCamera(position, this);
    this->m_surface = new QtCameraSurface(this);
    this->m_camera->setViewfinder(this->m_surface);
    this->m_camera->setCaptureMode(QCamera::CaptureViewfinder);

    QCameraViewfinderSettings viewSettings = this->m_camera->viewfinderSettings();
    viewSettings.setMaximumFrameRate(30);
    viewSettings.setPixelFormat(QVideoFrame::Format_RGB24);
    viewSettings.setPixelAspectRatio(4, 3);
    viewSettings.setResolution(this->width(), this->height());
    this->m_camera->setViewfinderSettings(viewSettings);
    this->m_camera->start();
}

size_t QtCameraSource::width()
{
    return 640;
}

size_t QtCameraSource::height()
{
    return 400;
}

void QtCameraSource::start()
{
    qDebug() << "Starting camera";
    //this->m_camera->start();
}

void QtCameraSource::requestFrame()
{
    QImage lastCapture = this->m_surface->lastCapture();
    emit captured(QByteArray::fromRawData((const char*)lastCapture.bits(),
                                          lastCapture.sizeInBytes()));
}

void QtCameraSource::stop()
{
    this->m_camera->stop();
}
