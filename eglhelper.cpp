#include <QDebug>

#include "eglhelper.h"

#include <mir_toolkit/mir_client_library.h>

bool initEgl(EGLContext* eglContext, EGLDisplay* eglDisplay)
{
    EGLDisplay display;
    EGLContext context;
    EGLConfig eglConfig;

    MirConnection* connection = mir_connect_sync("/run/user/32011/mir_socket", "optic");
    if (!mir_connection_is_valid(connection))
    {
        qWarning() << "could not connect to server\n";
        return false;
    }

    EGLNativeDisplayType nativeDisplay = (EGLNativeDisplayType) mir_connection_get_egl_native_display(connection);
    display = eglGetDisplay(nativeDisplay);
    if (display == EGL_NO_DISPLAY) {
        qWarning() << "No EGL display found.";
        return false;
    }

    EGLint major;
    EGLint minor;
    eglInitialize(display, &major, &minor);

    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    int config;
    eglChooseConfig(display, attribs, &eglConfig, 1, &config);

    if (config == 0) {
        qWarning() << "No EGL config found:" << config;
        return false;
    }

    context = eglCreateContext(display, eglConfig, 0, context_attributes);
    if (context == EGL_NO_CONTEXT) {
        qWarning() << "No context created.";
        return false;
    }

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
    *eglContext = context;
    *eglDisplay = display;
    return true;
}

void provideFramebuffer(GLuint* fbo)
{
    glGenFramebuffers(1, fbo);
    qDebug() << "New fbo:" << *fbo;
}

void provideTexture(GLuint* texture)
{
    glGenTextures(1, texture);
    qDebug() << "New texture:" << *texture;
}
