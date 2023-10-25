#ifndef HYBRISCAMERASOURCE_H
#define HYBRISCAMERASOURCE_H

#include <QObject>
#include <QByteArray>
#include <QDebug>
#include <QMutex>
#include <QString>
#include <QTimer>

#include <hybris/camera/camera_compatibility_layer.h>
#include <hybris/camera/camera_compatibility_layer_capabilities.h>

#include "eglhelper.h"

struct HybrisCameraInfo {
    int id = -1;
    QString description;
    int facingDirection;
    int orientation;
};

class HybrisCameraSource : public QObject
{
    Q_OBJECT

public:
    static QVector<HybrisCameraInfo> availableCameras();

    explicit HybrisCameraSource(HybrisCameraInfo info = HybrisCameraInfo(),
                                EGLContext eglContext = EGL_NO_CONTEXT,
                                EGLDisplay eglDisplay = EGL_NO_DISPLAY,
                                EGLSurface eglSurface = EGL_NO_SURFACE,
                                QObject *parent = nullptr);
    ~HybrisCameraSource();
    void start();
    void stop();
    Q_INVOKABLE void requestFrame();

    void setSize(const size_t& width, const size_t& height);
    size_t width();
    size_t height();

    QMutex* bufferMutex();

private slots:
    void queueStart();
    void queueDelayedStop();

private:
    HybrisCameraInfo m_info;
    CameraControl* m_control = nullptr;
    CameraControlListener* m_listener = nullptr;

    size_t m_width = 0;
    size_t m_height = 0;
    short m_fps;
    GLuint m_fbo;
    GLuint m_texture;
    QMutex m_bufferMutex;
    QByteArray m_intermediateBuffer;
    QByteArray m_pixelBuffer;
    EGLContext m_eglContext;
    EGLDisplay m_eglDisplay;
    EGLSurface m_eglSurface;
    QTimer m_stopDelayer;

signals:
    void captured(QByteArray frame);
};

#endif // HYBRISCAMERASOURCE_H
