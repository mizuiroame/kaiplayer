#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
int main(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = get ? get(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
    if (!eglInitialize(d, NULL, NULL)) { printf("no EGL\n"); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE}, n;
    EGLConfig c; eglChooseConfig(d, ca, &c, 1, &n);
    EGLint xa[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, x);
    printf("%s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return 0;
}
