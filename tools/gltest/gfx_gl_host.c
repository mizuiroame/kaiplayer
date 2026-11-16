// The GPU backend on the host, for checking it against the CPU one: the UI
// preview calls gfxSetTarget(pixels) before drawing a scene and
// gfxSetTarget(NULL) after; here that draws into an offscreen framebuffer
// with Mesa's OpenGL ES and reads the result back into pixels.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>

#include "gfx.h"
#include "gfx_gl.h"

static uint32_t *g_target;
static uint32_t g_stride;
static GLuint g_fbo;

static bool setup(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay display = get ? get(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
    if (!eglInitialize(display, NULL, NULL))
        return false;
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint config_attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_NONE};
    EGLConfig config;
    EGLint count;
    eglChooseConfig(display, config_attributes, &config, 1, &count);
    EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return false;
    GLuint color;
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, GFX_FB_WIDTH, GFX_FB_HEIGHT);
    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return false;
    return gfxGlSetup();
}

void gfxSetTarget(uint32_t *pixels, uint32_t stride)
{
    static bool ready, failed;
    if (!ready && !failed) {
        ready = setup();
        failed = !ready;
        if (failed)
            fprintf(stderr, "gltest: no OpenGL ES (0x%x)\n", (unsigned)glGetError());
    }
    if (!ready)
        return;
    if (pixels) {
        g_target = pixels;
        g_stride = stride;
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        gfxGlBeginFrame(GFX_FB_WIDTH, GFX_FB_HEIGHT);
        return;
    }
    gfxGlEndFrame();
    // GL's rows go bottom up.
    static uint32_t rows[GFX_FB_WIDTH * GFX_FB_HEIGHT];
    glReadPixels(0, 0, GFX_FB_WIDTH, GFX_FB_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rows);
    for (int y = 0; y < GFX_FB_HEIGHT; y++)
        for (int x = 0; x < GFX_FB_WIDTH; x++)
            g_target[(size_t)y * g_stride + x] = rows[(size_t)(GFX_FB_HEIGHT - 1 - y) * GFX_FB_WIDTH + x] | 0xFF000000u;
    g_target = NULL;
}
