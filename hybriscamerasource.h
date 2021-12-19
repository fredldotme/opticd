#ifndef HYBRISCAMERASOURCE_H
#define HYBRISCAMERASOURCE_H

#include <QObject>
#include <QByteArray>
#include <QDebug>
#include <QMutex>
#include <QString>

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
                                QObject *parent = nullptr);
    ~HybrisCameraSource();
    void start();
    void requestFrame();
    void stop();

    Q_INVOKABLE void updatePreview();

    size_t width();
    size_t height();

    EGLContext eglContext();
    EGLDisplay eglDisplay();
    GLuint fbo();
    GLuint texture();
    uint8_t* pixelBuffer();
    QMutex* bufferMutex();

private:
    CameraControl* m_control = nullptr;
    CameraControlListener* m_listener = nullptr;

    size_t m_width;
    size_t m_height;
    short m_fps;
    GLuint m_fbo;
    GLuint m_texture;
    QMutex m_bufferMutex;
    QByteArray m_pixelBuffer;
    EGLContext m_eglContext;
    EGLDisplay m_eglDisplay;

signals:
    void captured(QByteArray frame);
};

#endif // HYBRISCAMERASOURCE_H
