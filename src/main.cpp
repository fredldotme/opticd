#include <QCoreApplication>
#include <QDebug>
#include <QDirIterator>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <memory>
#include <set>

#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <sys/types.h>

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

static void cleanup()
{
    // Stop bridges after quit is requested.
    // Stop the source first as destroying the video node while frames
    // are being fed into it won't allow deletion of the sink.
    QMutexLocker locker(&cleanupMutex);
    for (SourceSinkPair& bridge : bridges) {
        delete bridge.source;
        bridge.source = nullptr;
        delete bridge.sink;
        bridge.sink = nullptr;
    }
}

static void sig_handler(int sig_num)
{
    qInfo("Quitting...");
    cleanup();
    exit(0);
}

int main(int argc, char *argv[])
{
    // Get to the chopper
    chdir("/");

    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;

    QCoreApplication a(argc, argv);

    bool initSuccess = initEgl(&context, &display, &surface);
    if (!initSuccess) {
        qFatal("EGL not initialized");
        return 1;
    }

    signal(SIGINT, sig_handler);

    for (const HybrisCameraInfo &cameraInfo : HybrisCameraSource::availableCameras()) {
        HybrisCameraSource* source = new HybrisCameraSource(cameraInfo,
                                                            context,
                                                            display,
                                                            surface);
        V4L2LoopbackSink* sink = new V4L2LoopbackSink(source->width(),
                                                      source->height(),
                                                      cameraInfo.description);

        // Cause open() on devices to start frame feed
        QObject::connect(sink, &V4L2LoopbackSink::deviceCreated,
                         source, &HybrisCameraSource::start, Qt::DirectConnection);
        QObject::connect(sink, &V4L2LoopbackSink::deviceCreated,
                         sink, &V4L2LoopbackSink::feedDummyFrame, Qt::DirectConnection);
        QObject::connect(sink, &V4L2LoopbackSink::deviceRemoved,
                         source, &HybrisCameraSource::stop, Qt::DirectConnection);

        // Frame passing through one-way communication from source to sink
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
