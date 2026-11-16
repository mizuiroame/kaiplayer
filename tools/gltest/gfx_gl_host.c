// The GPU backend on the host, for checking it against the CPU one: the UI
// preview calls gfxSetTarget(pixels) before drawing a scene and
// gfxSetTarget(NULL) after; here that draws into an offscreen framebuffer
// with Mesa's OpenGL ES and reads the result back into pixels.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "gfx_gl.h"

static uint32_t *g_target;
static uint32_t g_stride;
static GLuint g_fbo;
static int g_width = GFX_FB_WIDTH, g_height = GFX_FB_HEIGHT;  // GLTEST_HEIGHT=1440 draws as docked at 1440p.
static int g_frames;

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
    const char *height = getenv("GLTEST_HEIGHT");
    if (height && atoi(height) > 0) {
        g_height = atoi(height);
        g_width = g_height * 16 / 9;
    }
    GLuint color;
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, g_width, g_height);
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
        // GLTEST_DOCKED_LATER=1: the first scene at 1080p, then the rest at the
        // larger size, as when the console goes from handheld to the dock.
        bool handheld_first = getenv("GLTEST_DOCKED_LATER") && g_frames == 0 && g_height != GFX_FB_HEIGHT;
        gfxGlBeginFrame(handheld_first ? GFX_FB_WIDTH : g_width, handheld_first ? GFX_FB_HEIGHT : g_height);
        return;
    }
    gfxGlEndFrame();
    // GL's rows go bottom up. At another size, the frame is also saved whole
    // (tools/gltest/build/hi_frameNN.ppm) and the 1920x1080 target gets its nearest pixels.
    static uint32_t rows[3840 * 2160];
    glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE, rows);
    for (int y = 0; y < GFX_FB_HEIGHT; y++)
        for (int x = 0; x < GFX_FB_WIDTH; x++) {
            int sy = g_height - 1 - y * g_height / GFX_FB_HEIGHT, sx = x * g_width / GFX_FB_WIDTH;
            g_target[(size_t)y * g_stride + x] = rows[(size_t)sy * g_width + sx] | 0xFF000000u;
        }
    if (g_height != GFX_FB_HEIGHT) {
        char path[64];
        snprintf(path, sizeof(path), "../../tools/gltest/build/hi_frame%02d.ppm", g_frames++);  // Run from tests/preview.
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", g_width, g_height);
            static uint8_t line[3840 * 3];
            for (int y = g_height - 1; y >= 0; y--) {
                for (int x = 0; x < g_width; x++)
                    memcpy(line + x * 3, &rows[(size_t)y * g_width + x], 3);
                fwrite(line, 3, (size_t)g_width, f);
            }
            fclose(f);
        }
    }
    g_target = NULL;
}
