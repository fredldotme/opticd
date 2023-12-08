#include "hybriscamerasource.h"

#include <QMutexLocker>
#include <QMetaObject>

// Default camera names, assumes a max of 2 right now
const QString DESCRIPTION_FRONT = QStringLiteral("Front-facing camera");
const QString DESCRIPTION_BACK = QStringLiteral("Back-facing camera");

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
        if (i % 4 == 3) continue;
        to[toLength++] = from[i];
    }
}

static void readTextureIntoBuffer(void* ctx)
{
    HybrisCameraSource* thiz = static_cast<HybrisCameraSource*>(ctx);

    QMetaObject::invokeMethod(thiz, "requestFrame", Qt::QueuedConnection);
}

static void setPreviewSize(void* ctx, int width, int height)
{
    HybrisCameraSource* thiz = static_cast<HybrisCameraSource*>(ctx);

    thiz->setSize(width, height);
}

HybrisCameraSource::HybrisCameraSource(HybrisCameraInfo info, EGLContext context,
                                       EGLDisplay display, EGLSurface surface, QObject *parent) :
    QObject(parent),
    m_listener(new CameraControlListener),
    m_eglContext(context),
    m_eglDisplay(display),
    m_eglSurface(surface)
{
    if (info.id < 0)
        return;

    memset(this->m_listener, 0, sizeof(*this->m_listener));
    this->m_listener->context = this;
    this->m_listener->on_preview_texture_needs_update_cb = &readTextureIntoBuffer;

    this->m_control = android_camera_connect_by_id(info.id, this->m_listener);
    if (!this->m_control) {
        qWarning() << "Failed to connect to camera" << info.id << info.description;
        return;
    }

    // Delay stop of frame production
    // Applications tend to query the device and only see it as valid when
    // it receives frames from the V4L2 device.
    // To have repeated accesses not be disturbed, just delay stopping of the
    // actual feed.
    this->m_stopDelayer.setSingleShot(true);
    this->m_stopDelayer.setInterval(3000);
    QObject::connect(&this->m_stopDelayer, &QTimer::timeout,
                     this, [=](){
        qDebug() << "... stopping camera now!";
        android_camera_stop_preview(this->m_control);
    });

    android_camera_enumerate_supported_preview_sizes(this->m_control, &setPreviewSize, this);
    android_camera_set_preview_size(this->m_control, this->width(), this->height());

    this->m_intermediateBuffer.resize(this->width() * this->height() * 4);
    this->m_pixelBuffer.resize(this->width() * this->height() * 3);

    int min, max;
    android_camera_get_preview_fps_range(this->m_control, &min, &max);
    android_camera_set_preview_fps(this->m_control, min);
    android_camera_set_preview_callback_mode(this->m_control, PREVIEW_CALLBACK_ENABLED);

    android_camera_set_preview_format(this->m_control, CAMERA_PIXEL_FORMAT_RGBA8888);
    provideExternalTexture(&this->m_texture);
    provideFramebuffer(&this->m_fbo);

    glBindFramebuffer(GL_FRAMEBUFFER, this->m_fbo);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, this->m_texture);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_EXTERNAL_OES, this->m_texture, 0);

    android_camera_set_preview_texture(this->m_control, this->m_texture);
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
    // Don't support anything higher than 720p for now
    if (width > 1280 || height > 720)
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

QMutex* HybrisCameraSource::bufferMutex()
{
    return &this->m_bufferMutex;
}

void HybrisCameraSource::start()
{
    if (!this->m_control)
        return;

    QMetaObject::invokeMethod(this, "queueStart", Qt::QueuedConnection);
}

void HybrisCameraSource::queueStart()
{
    this->m_stopDelayer.stop();

    qDebug() << "Starting camera";
    android_camera_start_preview(this->m_control);
}

void HybrisCameraSource::requestFrame()
{
    QMutexLocker locker(&this->m_bufferMutex);

    const bool mcSuccess = eglMakeCurrent(this->m_eglDisplay, this->m_eglSurface, this->m_eglSurface, this->m_eglContext);
    if (!mcSuccess) {
        qWarning() << "Failed to make current" << eglGetError();
        return;
    }

    glActiveTexture(GL_TEXTURE1);

    android_camera_update_preview_texture(this->m_control);

    glBindFramebuffer(GL_FRAMEBUFFER, this->m_fbo);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, this->m_texture);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_EXTERNAL_OES, this->m_texture, 0);
    glReadPixels(0, 0, this->width(), this->height(), GL_RGBA, GL_UNSIGNED_BYTE, (char*)this->m_intermediateBuffer.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    removeAlpha((uint8_t*)this->m_intermediateBuffer.data(), (uint8_t*)this->m_pixelBuffer.data(), this->m_intermediateBuffer.length());
    emit captured(this->m_pixelBuffer);
}

void HybrisCameraSource::stop()
{
    if (!this->m_control)
        return;

    QMetaObject::invokeMethod(this, "queueDelayedStop", Qt::QueuedConnection);
}

void HybrisCameraSource::queueDelayedStop()
{
    qInfo() << "Stopping camera soon...";
    this->m_stopDelayer.stop();
    this->m_stopDelayer.start();
}
