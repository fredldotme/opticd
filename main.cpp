#include <QCoreApplication>
#include <QCameraInfo>
#include <QPair>
#include <QThread>
#include <QVector>

#include <signal.h>

#include "qtcamerasource.h"
#include "v4l2loopbacksink.h"

struct SourceSinkPair {
    QtCameraSource* source = nullptr;
    V4L2LoopbackSink* sink = nullptr;
    QThread* sinkThread = nullptr;
};

void sig_handler(int sig_num)
{
    qApp->exit(0);
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    signal(SIGINT, sig_handler);

    // Query available cameras and create the bridges
    QVector<SourceSinkPair> bridges;

    for (const QCameraInfo &cameraInfo : QCameraInfo::availableCameras()) {
        QtCameraSource* source = new QtCameraSource(cameraInfo.position());
        QThread* sinkThread = new QThread();
        V4L2LoopbackSink* sink = new V4L2LoopbackSink(source->width(),
                                                      source->height(),
                                                      cameraInfo.description());
        sink->moveToThread(sinkThread);

        // Open and close device on demand
        QObject::connect(sink, &V4L2LoopbackSink::deviceOpened,
                         source, &QtCameraSource::start);
        QObject::connect(sink, &V4L2LoopbackSink::deviceClosed,
                         source, &QtCameraSource::stop);

        // Frame passing through two-way communication between sink and source
        QObject::connect(sink, &V4L2LoopbackSink::frameRequested,
                         source, &QtCameraSource::requestFrame, Qt::DirectConnection);
        QObject::connect(source, &QtCameraSource::captured,
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

    // Stop bridges after quit is requested
    for (SourceSinkPair& bridge : bridges) {
        bridge.sinkThread->terminate();
        bridge.sinkThread->wait();
        delete bridge.sink;
        bridge.sink = nullptr;
        delete bridge.source;
        bridge.source = nullptr;
        delete bridge.sinkThread;
        bridge.sinkThread = nullptr;
    }

    return ret;
}
