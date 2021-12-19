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

        qDebug() << "Found camera:" << info.description << "orientation:" << info.orientation;

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
    glReadPixels(0, 0, thiz->width(), thiz->height(), GL_RGBA, GL_UNSIGNED_BYTE, thiz->intermediateBuffer());
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    removeAlpha(thiz->intermediateBuffer(), thiz->pixelBuffer(), rgbaSize);

    QMetaObject::invokeMethod(thiz, "updatePreview", Qt::QueuedConnection);
    QMetaObject::invokeMethod(thiz, "requestFrame", Qt::QueuedConnection);
}

static void setPreviewSize(void* ctx, int width, int height)
{
    HybrisCameraSource* thiz = static_cast<HybrisCameraSource*>(ctx);

    thiz->setSize(width, height);
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

    android_camera_enumerate_supported_preview_sizes(this->m_control, &setPreviewSize, this);
    android_camera_set_preview_size(this->m_control, this->width(), this->height());

    this->m_intermediateBuffer.resize(this->width() * this->height() * 4);
    this->m_pixelBuffer.resize(this->width() * this->height() * 3);

    int min, max;
    android_camera_get_preview_fps_range(this->m_control, &min, &max);
    android_camera_set_preview_fps(this->m_control, min);
    android_camera_set_preview_callback_mode(this->m_control, PREVIEW_CALLBACK_ENABLED);

    android_camera_set_preview_format(this->m_control, CAMERA_PIXEL_FORMAT_RGBA8888);
    provideFramebuffer(&this->m_fbo);
    provideTexture(&this->m_texture);
    android_camera_set_preview_texture(this->m_control, this->texture());
}

HybrisCameraSource::~HybrisCameraSource()
{
    if (this->m_control) {
        android_camera_disconnect(this->m_control);
        android_camera_delete(this->m_control);
        this->m_control = nullptr;
    }

    if (this->m_listener) {
        delete this->m_listener;
        this->m_listener = nullptr;
    }
}

void HybrisCameraSource::setSize(const size_t &width, const size_t &height)
{
    if (width > 1920 && height > 1080)
        return;

    if (width <= this->m_width && height <= this->m_height)
        return;

    this->m_width = width;
    this->m_height = height;
}

size_t HybrisCameraSource::width()
{
    return this->m_width;
}

size_t HybrisCameraSource::height()
{
    return this->m_height;
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

uint8_t* HybrisCameraSource::intermediateBuffer()
{
    return (uint8_t*)this->m_intermediateBuffer.data();
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
