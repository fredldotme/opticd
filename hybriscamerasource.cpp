#include "hybriscamerasource.h"

#include <QMutexLocker>
#include <QMetaObject>

// Default camera names, assumes a max of 2 right now
const QString DESCRIPTION_FRONT = QStringLiteral("Front facing camera");
const QString DESCRIPTION_BACK = QStringLiteral("Back facing camera");

const int ANDROID_OK = 0;

QVector<HybrisCameraInfo> HybrisCameraSource::availableCameras()
{
    QVector<HybrisCameraInfo> ret;
    static int unknownCameraCounter = 0;

    const int numberOfCameras = android_camera_get_number_of_devices();
    for (int id = 0; id < numberOfCameras; id++) {
        HybrisCameraInfo info;
        const int status = android_camera_get_device_info(id, &info.facingDirection, &info.orientation);

        if (status != ANDROID_OK)
            continue;

        if (info.facingDirection == BACK_FACING_CAMERA_TYPE)
            continue;

        switch(info.facingDirection) {
        case FRONT_FACING_CAMERA_TYPE:
            info.description = DESCRIPTION_FRONT;
            break;
        case BACK_FACING_CAMERA_TYPE:
            info.description = DESCRIPTION_BACK;
            break;
        default:
            info.description = QStringLiteral("Unknown camera %1").arg(++unknownCameraCounter);
            break;
        }

        info.id = id;
        ret.push_back(info);
    }

    return ret;
}

static void removeAlpha(uint8_t* from, uint8_t* to, size_t fromLength)
{
    size_t toLength = 0;
    for (int i = 0; i < fromLength; i++) {
        to[toLength] = from[i];
        if (i % 3 == 0)
            ++toLength;
    }
}

static void readTextureIntoBuffer(void* ctx)
{
    HybrisCameraSource* thiz = static_cast<HybrisCameraSource*>(ctx);

    QMutexLocker locker(thiz->bufferMutex());

    eglMakeCurrent(thiz->eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, thiz->eglContext());
    GLuint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *) &prevFbo);

    glBindFramebuffer(GL_FRAMEBUFFER, thiz->fbo());
    glBindTexture(GL_TEXTURE_2D, thiz->texture());

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, thiz->texture(), 0);

    const size_t rgbaSize = thiz->width() * thiz->height() * 4;
    uint8_t* rgbaBuffer = new uint8_t[rgbaSize];
    glReadPixels(0, 0, thiz->width(), thiz->height(), GL_RGBA, GL_UNSIGNED_BYTE, rgbaBuffer);
    //glReadPixels(0, 0, thiz->width(), thiz->height(), GL_RGBA, GL_UNSIGNED_BYTE, thiz->pixelBuffer());
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    removeAlpha(rgbaBuffer, thiz->pixelBuffer(), rgbaSize);

    //rgb2yuv420p((char*)rgbBuffer, (char*)thiz->pixelBuffer(), thiz->width(), thiz->height());
    delete[] rgbaBuffer;

    QMetaObject::invokeMethod(thiz, "updatePreview", Qt::QueuedConnection);
    QMetaObject::invokeMethod(thiz, "requestFrame", Qt::QueuedConnection);
}

HybrisCameraSource::HybrisCameraSource(HybrisCameraInfo info, EGLContext context, EGLDisplay display, QObject *parent) :
    QObject(parent),
    m_listener(new CameraControlListener),
    m_eglContext(context),
    m_eglDisplay(display)
{
    if (info.id < 0)
        return;

    this->m_control = android_camera_connect_by_id(info.id, this->m_listener);
    if (!this->m_control) {
        qWarning() << "Failed to connect to camera" << info.id << info.description;
        return;
    }

    this->m_listener->context = this;
    this->m_listener->on_preview_texture_needs_update_cb = &readTextureIntoBuffer;

    //android_camera_enumerate_supported_preview_sizes(this->m_control);
    //this->m_pixelBuffer = new uint8_t[this->width() * this->height() * 3 / 2];
    //this->m_pixelBuffer = new uint8_t[this->width() * this->height() * 3];
    this->m_pixelBuffer.resize(this->width() * this->height() * 3);
    android_camera_set_preview_size(this->m_control, this->width(), this->height());
    provideFramebuffer(&this->m_fbo);
    provideTexture(&this->m_texture);
    android_camera_set_preview_texture(this->m_control, this->texture());
}

HybrisCameraSource::~HybrisCameraSource()
{
    if (this->m_control) {
        android_camera_disconnect(this->m_control);
        this->m_control = nullptr;
    }

    if (this->m_listener) {
        delete this->m_listener;
        this->m_listener = nullptr;
    }
}

size_t HybrisCameraSource::width()
{
    return 1280;
}

size_t HybrisCameraSource::height()
{
    return 720;
}

EGLContext HybrisCameraSource::eglContext()
{
    return this->m_eglContext;
}

EGLDisplay HybrisCameraSource::eglDisplay()
{
    return this->m_eglDisplay;
}

GLuint HybrisCameraSource::fbo()
{
    return this->m_fbo;
}

GLuint HybrisCameraSource::texture()
{
    return this->m_texture;
}

uint8_t* HybrisCameraSource::pixelBuffer()
{
    return (uint8_t*)this->m_pixelBuffer.data();
}

QMutex* HybrisCameraSource::bufferMutex()
{
    return &this->m_bufferMutex;
}

void HybrisCameraSource::start()
{
    if (!this->m_control)
        return;

    qDebug() << "Starting camera";
    android_camera_set_preview_fps(this->m_control, 30);
    android_camera_start_preview(this->m_control);
}

void HybrisCameraSource::requestFrame()
{
    QMutexLocker locker(&this->m_bufferMutex);
    emit captured(this->m_pixelBuffer);
}

void HybrisCameraSource::stop()
{
    if (!this->m_control)
        return;

    android_camera_stop_preview(this->m_control);
}

void HybrisCameraSource::updatePreview()
{
    if (!this->m_control)
        return;

    android_camera_update_preview_texture(this->m_control);
}
