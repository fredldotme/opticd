#include <QCoreApplication>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QThread>
#include <QVector>

#include <signal.h>

#include "eglhelper.h"
#include "hybriscamerasource.h"
#include "v4l2loopbacksink.h"

struct SourceSinkPair {
    HybrisCameraSource* source = nullptr;
    V4L2LoopbackSink* sink = nullptr;
};

// Query available cameras and create the bridges
QVector<SourceSinkPair> bridges;
QMutex cleanupMutex;

void cleanup()
{
    // Stop bridges after quit is requested
    QMutexLocker locker(&cleanupMutex);
    for (SourceSinkPair& bridge : bridges) {
        delete bridge.sink;
        bridge.sink = nullptr;
        delete bridge.source;
        bridge.source = nullptr;
    }
}

void sig_handler(int sig_num)
{
    qInfo("Quitting...");
    cleanup();
    exit(0);
}

int main(int argc, char *argv[])
{
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;

    QCoreApplication a(argc, argv);

    bool initSuccess = initEgl(&context, &display, &surface);
    if (!initSuccess) {
        qWarning() << "EGL not initialized";
        return 1;
    }

    signal(SIGINT, sig_handler);

    int counter = 0;
    for (const HybrisCameraInfo &cameraInfo : HybrisCameraSource::availableCameras()) {
        HybrisCameraSource* source = new HybrisCameraSource(cameraInfo,
                                                            context,
                                                            display,
                                                            surface);
        V4L2LoopbackSink* sink = new V4L2LoopbackSink(source->width(),
                                                      source->height(),
                                                      cameraInfo.description);

        // TODO: Open and close device on demand
        QObject::connect(sink, &V4L2LoopbackSink::deviceOpened,
                         source, &HybrisCameraSource::start);
        QObject::connect(sink, &V4L2LoopbackSink::deviceClosed,
                         source, &HybrisCameraSource::stop);

        // TODO: Only produce frames when the PID is allowed to access the camera

        // Frame passing through two-way communication between sink and source
        QObject::connect(sink, &V4L2LoopbackSink::frameRequested,
                         source, &HybrisCameraSource::requestFrame, Qt::DirectConnection);
        QObject::connect(source, &HybrisCameraSource::captured,
                         sink, &V4L2LoopbackSink::pushCapture, Qt::DirectConnection);

        sink->run();

        SourceSinkPair bridge;
        bridge.source = source;
        bridge.sink = sink;
        bridges.push_back(bridge);
    }

    // Run the service
    int ret = a.exec();

    cleanup();

    return ret;
}
