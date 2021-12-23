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
    HybrisCameraSource* source = nullptr;
    V4L2LoopbackSink* sink = nullptr;
};

// Query available cameras and create the bridges
QVector<SourceSinkPair> bridges;
QMutex cleanupMutex;

static void cleanup()
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

static void sig_handler(int sig_num)
{
    qInfo("Quitting...");
    cleanup();
    exit(0);
}

static inline void setCapsOrDie()
{
    static const std::array<cap_value_t, 4> necessaryCaps {
        CAP_CHOWN, CAP_SETUID, CAP_SETGID, CAP_SYS_PTRACE
    };
    cap_t caps = cap_init();

    // Auto-free caps object when done
    auto capfree = [](cap_t* c){ cap_free(c); };
    std::unique_ptr<cap_t, decltype(capfree)> capcleaner(&caps, capfree);

    if (cap_set_flag(caps, CAP_PERMITTED, necessaryCaps.size(), necessaryCaps.data(), CAP_SET) != 0) {
        qFatal("Failed to set permitted caps: %s", strerror(errno));
        exit(1);
        return;
    }
    if (cap_set_flag(caps, CAP_EFFECTIVE, necessaryCaps.size(), necessaryCaps.data(), CAP_SET) != 0) {
        qFatal("Failed to set effective caps: %s", strerror(errno));
        exit(1);
        return;
    }
    if (cap_set_proc(caps) != 0) {
        qFatal("Failed to set process caps: %s", strerror(errno));
        exit(1);
        return;
    }
}

static inline void dropPrivsOrDie()
{
    const int groupId = getuid();
    const int userId = getuid();
    static const std::vector<std::string> necessaryGroups = {
        "video", "android_graphics"
    };

    qInfo("Running as uid: %d", userId);
    qInfo("Euid: %d", geteuid());

    if (geteuid() == 0) {
        int ngroups = sysconf(_SC_NGROUPS_MAX) + 1;
        gid_t* groups = (gid_t*) malloc(ngroups * sizeof(gid_t));

        std::vector<gid_t> newGroups;

        struct passwd* pw = getpwuid(userId);
        if (!pw) {
            qFatal("Failed to get passwd entry: %s", strerror(errno));
            exit(1);
            return;
        }

        if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) == -1) {
            qFatal("getgrouplist() returned -1; ngroups = %d", ngroups);
            exit(1);
            return;
        }

        for (int i = 0; i < ngroups; i++) {
            gid_t& group = groups[i];
            struct group* groupStruct = getgrgid(group);
            if (!groupStruct) {
                qFatal("Failed to get group name for gid %d", group);
                exit(1);
                return;
            }

            if (std::find(necessaryGroups.begin(), necessaryGroups.end(),
                          std::string(groupStruct->gr_name)) != necessaryGroups.end()) {
                newGroups.push_back(group);
            }
        }

        if (newGroups.size() != necessaryGroups.size()) {
            qFatal("Not enough sauce.");
            exit(1);
            return;
        }

        if (setgroups(newGroups.size(), newGroups.data()) != 0) {
            qFatal("Failed to set new supplementary groups, bailing...");
            exit(1);
            return;
        }

        free(groups);

        if (setgid(groupId) != 0) {
            qFatal("Failed to drop group privileges: %s", strerror(errno));
            exit(1);
            return;
        }
        if (setuid(userId) != 0) {
            qFatal("Failed to drop user privileges: %s", strerror(errno));
            exit(1);
            return;
        }
    }

    if (setuid(0) != -1) {
        qFatal("Managed to regain root privileges, bailing...");
        exit(1);
        return;
    }

    // Lomiri needs to read the environment or arguments of this process...
    // ... fine! We still have CAP_CHOWN.
    if (prctl(PR_SET_DUMPABLE, 1) < 0) {
        qFatal("Failed to set dumpable flag: %s", strerror(errno));
        exit(1);
        return;
    }
}

int main(int argc, char *argv[])
{
    // Before doing ANYTHING, keep PTRACE capability and drop privileges
    setCapsOrDie();
    dropPrivsOrDie();

    // Get to the chopper
    chdir("/");

    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;

    // Paranoid power regain failed anyway, it's safe to allow suid
    QCoreApplication::setSetuidAllowed(true);

    QCoreApplication a(argc, argv);

    bool initSuccess = initEgl(&context, &display, &surface);
    if (!initSuccess) {
        qFatal("EGL not initialized");
        return 1;
    }

    signal(SIGINT, sig_handler);

    AccessMediator mediator;

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
