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
#include "accessmediator.h"
#include "hybriscamerasource.h"
#include "v4l2loopbacksink.h"

struct SourceSinkPair {
    std::shared_ptr<HybrisCameraSource> source;
    std::shared_ptr<V4L2LoopbackSink> sink;
};

static void sig_handler(int sig_num)
{
    qInfo("Quitting...");
    exit(0);
}

int main(int argc, char *argv[])
{
    // Get to the chopper
    chdir("/");

    // Query available cameras and create the bridges
    std::vector<SourceSinkPair> bridges;

    // This requires EGL
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;

    QCoreApplication a(argc, argv);

    const bool initSuccess = initEgl(&context, &display, &surface);
    if (!initSuccess) {
        qFatal("EGL not initialized");
        return 1;
    }

    signal(SIGINT, sig_handler);

    AccessMediator mediator;

    for (const HybrisCameraInfo &cameraInfo : HybrisCameraSource::availableCameras()) {
        auto source = std::make_shared<HybrisCameraSource>(cameraInfo,
                                                           context,
                                                           display,
                                                           surface);
        auto sink = std::make_shared<V4L2LoopbackSink>(source->width(),
                                                       source->height(),
                                                       cameraInfo.description);

        // Register created device with the mediator
        QObject::connect(sink, &V4L2LoopbackSink::deviceCreated,
                         &mediator, &AccessMediator::registerDevice, Qt::DirectConnection);
        QObject::connect(sink, &V4L2LoopbackSink::deviceRemoved,
                         &mediator, &AccessMediator::unregisterDevice, Qt::DirectConnection);

        // Cause open() on devices to start frame feed
        QObject::connect(&mediator, &AccessMediator::accessAllowed,
                         sink, &V4L2LoopbackSink::feedDummyFrame, Qt::DirectConnection);
        QObject::connect(&mediator, &AccessMediator::accessAllowed,
                         source, &HybrisCameraSource::start, Qt::DirectConnection);
        QObject::connect(&mediator, &AccessMediator::deviceClosed,
                         source, &HybrisCameraSource::stop, Qt::DirectConnection);

        // Frame passing through one-way communication from source to sink
        QObject::connect(source.get(), &HybrisCameraSource::captured,
                         sink.get(), &V4L2LoopbackSink::pushCapture, Qt::DirectConnection);

        sink->run();
        bridges.push_back({source, sink});
    }

    // Run the service
    int ret = a.exec();

    return ret;
}
