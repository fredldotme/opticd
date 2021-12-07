#include "v4l2loopbacksink.h"

#include <QFile>
#include <QThread>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "v4l2loopback.h"

#define CONTROLDEVICE "/dev/v4l2loopback"

V4L2LoopbackSink::V4L2LoopbackSink(size_t width,
                                   size_t height,
                                   QString description,
                                   QObject *parent) : QObject(parent),
    m_description(description),
    m_width(width),
    m_height(height)
{
}

V4L2LoopbackSink::~V4L2LoopbackSink()
{
    deleteLoopbackDevice();
}

void V4L2LoopbackSink::addLoopbackDevice()
{
    const int fd = open(CONTROLDEVICE, 0);
    if (fd < 0) {
        qWarning("Failed to open v4l2loopback control device");
        return;
    }

    v4l2_loopback_config cfg;
    snprintf(cfg.card_label, 32, "%s", this->m_description.toUtf8().data());
    cfg.capture_nr = -1;
    cfg.output_nr = -1;
    cfg.announce_all_caps = 1;
    cfg.max_width = this->m_width;
    cfg.max_height = this->m_height;
    cfg.max_buffers = 2;
    cfg.max_openers = 32;

    int ret = ioctl(fd, V4L2LOOPBACK_CTL_ADD, &cfg);
    if (ret < 0) {
        qWarning("Failed to add v4l2loopback device");
        close(fd);
        return;
    }

    this->m_path = QStringLiteral("/dev/video%1").arg(ret);
    this->m_deviceNumber = ret;
    this->m_vidsendsiz = this->m_width * this->m_height * 3;
    close(fd);

    qInfo("v4l2sink device '%s' created", this->m_path.toUtf8().data());

    // Let udev settle
    QThread::sleep(1);

    emit deviceOpened();
}

void V4L2LoopbackSink::deleteLoopbackDevice()
{
    const int fd = open(CONTROLDEVICE, 0);
    if (fd < 0) {
        qWarning("Failed to open v4l2loopback control device");
        return;
    }

    if (ioctl(fd, V4L2LOOPBACK_CTL_REMOVE, this->m_deviceNumber) < 0) {
        qWarning("Failed to delete v4l2sink device");
        return;
    }

    qInfo("v4l2sink device '%s' deleted", this->m_path.toUtf8().data());
    close(this->m_sinkFd);
    emit deviceClosed();
}

void V4L2LoopbackSink::openLoopbackDevice()
{
    this->m_sinkFd = open(this->m_path.toUtf8().data(), O_WRONLY);
    if (this->m_sinkFd < 0) {
        qWarning("Failed to open v4l2sink device. (%s)", strerror(errno));
        return;
    }

    // setup video for proper format
    struct v4l2_format v;
    int t;

    v.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    t = ioctl(this->m_sinkFd, VIDIOC_G_FMT, &v);
    if (t < 0) {
        qWarning("Failed to get current v4l2 sink format");
        return;
    }

    v.fmt.pix.width = this->m_width;
    v.fmt.pix.height = this->m_height;
    v.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    v.fmt.pix.sizeimage = this->m_vidsendsiz;
    t = ioctl(this->m_sinkFd, VIDIOC_S_FMT, &v);
    if (t < 0) {
        qWarning("Failed to set proper v4l2 sink format");
    }
}

void V4L2LoopbackSink::pushCapture(QByteArray capture)
{
    qDebug("Pushing capture");
    if (int written = write(this->m_sinkFd, capture.data(), capture.size()) != m_vidsendsiz) {
        qWarning("Failed to push captured frame, wrote %d/%d bytes, capture size %d", written, m_vidsendsiz, capture.size());
    }
}

void V4L2LoopbackSink::runLoop()
{
    addLoopbackDevice();
    openLoopbackDevice();

    this->m_running = true;
    while (this->m_running) {
        QThread::msleep(1000 / 30);
        emit frameRequested();
    }
}

void V4L2LoopbackSink::stopLoop()
{
    this->m_running = false;
}
