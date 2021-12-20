#ifndef EGLHELPER_H
#define EGLHELPER_H

#include <mir_toolkit/client_types.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

bool initEgl(EGLContext* eglContext, EGLDisplay* eglDisplay, EGLSurface* eglSurface);
void provideFramebuffer(GLuint* fbo);
void provideTexture(GLuint* texture);

#endif // EGLHELPER_H
