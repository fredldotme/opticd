#include <QDebug>

#include "eglhelper.h"

bool initEgl(EGLContext* eglContext, EGLDisplay* eglDisplay, EGLSurface* eglSurface)
{
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    EGLConfig eglConfig;

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
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
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
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

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, 1280,
        EGL_HEIGHT, 720,
        EGL_NONE
    };

    int config;
    eglChooseConfig(display, attribs, &eglConfig, 1, &config);

    if (config == 0) {
        qWarning() << "No EGL config found:" << config;
        return false;
    }

    surface = eglCreatePbufferSurface(display, eglConfig, pbufferAttribs);
    if (surface == EGL_NO_SURFACE) {
        qWarning() << "No surface created.";
        return false;
    }

    context = eglCreateContext(display, eglConfig, 0, context_attributes);
    if (context == EGL_NO_CONTEXT) {
        qWarning() << "No context created.";
        return false;
    }

    eglMakeCurrent(display, surface, surface, context);
    *eglContext = context;
    *eglDisplay = display;
    *eglSurface = surface;
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
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    qDebug() << "New texture:" << *texture << glGetError();
}

void provideExternalTexture(GLuint* texture)
{
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    qDebug() << "New external texture:" << *texture << glGetError();
}
