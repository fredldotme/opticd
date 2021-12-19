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
    QThread* sinkThread = nullptr;
};

// Query available cameras and create the bridges
QVector<SourceSinkPair> bridges;
QMutex cleanupMutex;

void cleanup()
{
    // Stop bridges after quit is requested
    QMutexLocker locker(&cleanupMutex);
    for (SourceSinkPair& bridge : bridges) {
        //bridge.sinkThread->terminate();
        //bridge.sinkThread->wait();
        delete bridge.sink;
        bridge.sink = nullptr;
        delete bridge.source;
        bridge.source = nullptr;
        delete bridge.sinkThread;
        bridge.sinkThread = nullptr;
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

    QCoreApplication a(argc, argv);

    bool initSuccess = initEgl(&context, &display);
    if (!initSuccess) {
        qWarning() << "EGL not initialized";
        return 1;
    }

    signal(SIGINT, sig_handler);

    int counter = 0;
    for (const HybrisCameraInfo &cameraInfo : HybrisCameraSource::availableCameras()) {
        HybrisCameraSource* source = new HybrisCameraSource(cameraInfo,
                                                            context,
                                                            display);
        QThread* sinkThread = new QThread();
        V4L2LoopbackSink* sink = new V4L2LoopbackSink(source->width(),
                                                      source->height(),
                                                      cameraInfo.description);
        sink->moveToThread(sinkThread);

        // Open and close device on demand
        QObject::connect(sink, &V4L2LoopbackSink::deviceOpened,
                         source, &HybrisCameraSource::start);
        QObject::connect(sink, &V4L2LoopbackSink::deviceClosed,
                         source, &HybrisCameraSource::stop);

        // Frame passing through two-way communication between sink and source
        QObject::connect(sink, &V4L2LoopbackSink::frameRequested,
                         source, &HybrisCameraSource::requestFrame, Qt::DirectConnection);
        QObject::connect(source, &HybrisCameraSource::captured,
                         sink, &V4L2LoopbackSink::pushCapture, Qt::DirectConnection);

        // Start the sink loop
        QObject::connect(sinkThread, &QThread::started,
                         sink, &V4L2LoopbackSink::runLoop, Qt::DirectConnection);
        sinkThread->start();

        SourceSinkPair bridge;
        bridge.source = source;
        bridge.sink = sink;
        bridge.sinkThread = sinkThread;
        bridges.push_back(bridge);
    }

    // Run the service
    int ret = a.exec();

    cleanup();

    return ret;
}
