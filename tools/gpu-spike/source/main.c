// A test of drawing with the GPU (OpenGL ES 3 through EGL and OunceVK) before
// KaiPlayer moves to it. Each frame draws about what KaiPlayer's busiest
// screens do: a full-screen picture, ~3000 rounded shapes with soft edges and
// a 600 px cover turning inside a disc. A bar at the top shows how long the
// frame took (green: in time for 60 fps). + exits.
//
// It writes sdmc:/kaiplayer-gpu-spike.log: the GPU, the resolution (and when
// it changes, docked or not), frame times every second, and each step of
// exiting, so a crash on exit shows where it happened.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#define LAYOUT_W 1280.0f
#define LAYOUT_H 720.0f
#define SHAPES 3000
#define MAX_QUADS (SHAPES + 16)
#define FLOATS_PER_VERTEX 12
#define COVER 600
#define BG_W 1920
#define BG_H 1080

static FILE *g_log;

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...)
{
    if (!g_log)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

typedef struct {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
} Window;

static bool createWindow(Window *w)
{
    w->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(w->display, NULL, NULL))
        return false;
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint config_attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE,
    };
    EGLConfig config;
    EGLint count;
    if (!eglChooseConfig(w->display, config_attributes, &config, 1, &count) || count == 0)
        return false;
    w->surface = eglCreateWindowSurface(w->display, config, nwindowGetDefault(), NULL);
    if (w->surface == EGL_NO_SURFACE)
        return false;
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    w->context = eglCreateContext(w->display, config, EGL_NO_CONTEXT, context_attributes);
    if (w->context == EGL_NO_CONTEXT)
        return false;
    eglMakeCurrent(w->display, w->surface, w->surface, w->context);
    eglSwapInterval(w->display, 1);
    return true;
}

// One shader for everything: a quad in layout units, with a texture or not,
// and a rounded-rectangle or ring shape cut out with a soft edge.
static const char *const VERTEX =
    "#version 300 es\n"
    "layout(location = 0) in vec2 position;\n"   // Layout units.
    "layout(location = 1) in vec2 uv;\n"
    "layout(location = 2) in vec4 color;\n"
    "layout(location = 3) in vec4 shape;\n"      // Half size (x, y), radius, kind.
    "uniform vec2 screen;\n"
    "uniform float scale;\n"                      // Pixels per layout unit.
    "out vec2 v_uv; out vec4 v_color; out vec4 v_shape; out vec2 v_local;\n"
    "void main() {\n"
    "    v_uv = uv; v_color = color; v_shape = shape;\n"
    "    v_local = (uv - 0.5) * 2.0 * shape.xy;\n"
    "    vec2 p = position * scale / screen * 2.0 - 1.0;\n"
    "    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
    "}\n";

static const char *const FRAGMENT =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv; in vec4 v_color; in vec4 v_shape; in vec2 v_local;\n"
    "uniform sampler2D image;\n"
    "uniform float scale;\n"
    "uniform float angle;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    float kind = v_shape.w;\n"
    "    vec4 c = v_color;\n"
    "    float edge = 1.0;\n"
    "    if (kind < 0.5) {\n"                                   // Rounded rectangle.
    "        vec2 q = abs(v_local) - v_shape.xy + v_shape.z;\n"
    "        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - v_shape.z;\n"
    "        edge = clamp(0.5 - d * scale, 0.0, 1.0);\n"
    "    } else if (kind < 1.5) {\n"                            // Picture, as is.
    "        c *= texture(image, v_uv);\n"
    "    } else {\n"                                            // Picture turned inside a disc with a hole.
    "        float r = length(v_local), outer = v_shape.x, hole = v_shape.z;\n"
    "        edge = clamp((outer - r) * scale + 0.5, 0.0, 1.0) * clamp((r - hole) * scale + 0.5, 0.0, 1.0);\n"
    "        float s = sin(angle), co = cos(angle);\n"
    "        vec2 t = mat2(co, s, -s, co) * (v_local / (2.0 * outer)) + 0.5;\n"
    "        c *= texture(image, t);\n"
    "    }\n"
    "    out_color = vec4(c.rgb, c.a * edge);\n"
    "}\n";

static GLuint compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        logf_("shader error: %s\n", log);
    }
    return shader;
}

static float g_vertices[MAX_QUADS * 6 * FLOATS_PER_VERTEX];
static int g_quads;

// A quad at x, y (layout), w x h, with a color and a shape (kind 0 rounded
// rect, 1 picture, 2 picture in a disc).
static void quad(float x, float y, float w, float h, const float color[4], float radius, float kind)
{
    if (g_quads >= MAX_QUADS)
        return;
    static const float CORNERS[6][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}};
    float *v = g_vertices + g_quads * 6 * FLOATS_PER_VERTEX;
    for (int i = 0; i < 6; i++, v += FLOATS_PER_VERTEX) {
        float u = CORNERS[i][0], t = CORNERS[i][1];
        float vertex[FLOATS_PER_VERTEX] = {x + u * w, y + t * h, u, t, color[0], color[1], color[2], color[3],
                                           w / 2.0f, h / 2.0f, radius, kind};
        memcpy(v, vertex, sizeof(vertex));
    }
    g_quads++;
}

static GLuint makeTexture(int w, int h, const uint32_t *pixels)
{
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

// Made-up pictures: a cover with stripes and a sun, and a dim background.
static void makePictures(uint32_t *cover, uint32_t *background)
{
    for (int y = 0; y < COVER; y++)
        for (int x = 0; x < COVER; x++) {
            float dx = x - 300.0f, dy = y - 260.0f;
            bool sun = dx * dx + dy * dy < 110.0f * 110.0f;
            unsigned r = sun ? 255 : 60 + y * 150 / COVER, g = sun ? 200 : 40 + (x * 3 + y) % 60,
                     b = sun ? 120 : 140 - y * 80 / COVER;
            if (!sun && (y / 20) % 3 == 0 && y > 360)
                r = 30, g = 140, b = 160;
            cover[y * COVER + x] = 0xFF000000u | b << 16 | g << 8 | r;
        }
    for (int y = 0; y < BG_H; y++)
        for (int x = 0; x < BG_W; x++) {
            unsigned r = 30 + x * 30 / BG_W, g = 22 + y * 20 / BG_H, b = 40 + (x + y) * 20 / (BG_W + BG_H);
            background[(size_t)y * BG_W + x] = 0xFF000000u | b << 16 | g << 8 | r;
        }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    g_log = fopen("sdmc:/kaiplayer-gpu-spike.log", "w");
    logf_("KaiPlayer GPU test\n");
    nwindowSetModeDimensions(nwindowGetDefault(), 0, 0, 0, 0);  // The console's resolution, docked or not.
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Window window;
    if (!createWindow(&window)) {
        logf_("EGL setup failed: 0x%x\n", eglGetError());
        fclose(g_log);
        return 1;
    }
    logf_("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    GLuint program = glCreateProgram();
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX), fs = compile(GL_FRAGMENT_SHADER, FRAGMENT);
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);
    GLint u_screen = glGetUniformLocation(program, "screen"), u_scale = glGetUniformLocation(program, "scale");
    GLint u_angle = glGetUniformLocation(program, "angle");

    uint32_t *cover = malloc(COVER * COVER * 4), *background = malloc((size_t)BG_W * BG_H * 4);
    makePictures(cover, background);
    GLuint cover_texture = makeTexture(COVER, COVER, cover), background_texture = makeTexture(BG_W, BG_H, background);
    free(cover);
    free(background);

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertices), NULL, GL_STREAM_DRAW);
    GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)(8 * sizeof(float)));
    for (int i = 0; i < 4; i++)
        glEnableVertexAttribArray(i);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    EGLint width = 0, height = 0;
    uint64_t second_start = armGetSystemTick(), last = second_start;
    int frames = 0;
    double worst = 0.0, total_ms = 0.0, shown_ms = 0.0;
    float angle = 0.0f;
    for (int seconds = 0; appletMainLoop();) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        EGLint w = 0, h = 0;
        eglQuerySurface(window.display, window.surface, EGL_WIDTH, &w);
        eglQuerySurface(window.display, window.surface, EGL_HEIGHT, &h);
        if (w != width || h != height) {
            width = w, height = h;
            logf_("resolution: %dx%d\n", width, height);
        }
        uint64_t start = armGetSystemTick();
        float scale = height / LAYOUT_H;
        glViewport(0, 0, width, height);
        glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform2f(u_screen, (float)width, (float)height);
        glUniform1f(u_scale, scale);
        angle += 0.25f * 6.2831853f / 60.0f;
        glUniform1f(u_angle, angle);

        // The background picture.
        static const float WHITE[4] = {1, 1, 1, 1};
        g_quads = 0;
        quad(0, 0, LAYOUT_W, LAYOUT_H, WHITE, 0, 1);
        glBindTexture(GL_TEXTURE_2D, background_texture);
        glBufferSubData(GL_ARRAY_BUFFER, 0, g_quads * 6 * stride, g_vertices);
        glDrawArrays(GL_TRIANGLES, 0, g_quads * 6);

        // Rows of rounded shapes, like list rows, buttons and pills.
        g_quads = 0;
        for (int i = 0; i < SHAPES; i++) {
            float x = (float)((i * 37) % 1200) + 20.0f, y = (float)((i * 53) % 660) + 30.0f;
            float c[4] = {0.3f + (i % 7) * 0.08f, 0.25f, 0.5f - (i % 5) * 0.06f, 0.55f};
            quad(x, y, 28.0f + (float)(i % 5) * 9.0f, 14.0f + (float)(i % 3) * 6.0f, c, 6.0f, 0);
        }
        // The frame-time bar: green in time for 60 fps, red not.
        float ok[4] = {0.2f, 0.85f, 0.4f, 1.0f}, late[4] = {0.95f, 0.3f, 0.25f, 1.0f};
        quad(20.0f, 8.0f, (float)(shown_ms * 20.0), 10.0f, shown_ms < 16.6 ? ok : late, 4.0f, 0);
        quad(20.0f + 16.6f * 20.0f, 4.0f, 2.0f, 18.0f, WHITE, 0.0f, 0);
        glBufferSubData(GL_ARRAY_BUFFER, 0, g_quads * 6 * stride, g_vertices);
        glDrawArrays(GL_TRIANGLES, 0, g_quads * 6);

        // A cover turning inside a disc, as KaiPlayer's CD.
        g_quads = 0;
        quad(96.0f, 140.0f, 400.0f, 400.0f, WHITE, 0.22f * 200.0f, 2);
        glBindTexture(GL_TEXTURE_2D, cover_texture);
        glBufferSubData(GL_ARRAY_BUFFER, 0, g_quads * 6 * stride, g_vertices);
        glDrawArrays(GL_TRIANGLES, 0, g_quads * 6);

        eglSwapBuffers(window.display, window.surface);
        uint64_t now = armGetSystemTick();
        double cpu_ms = armTicksToNs(now - start) / 1e6, frame_ms = armTicksToNs(now - last) / 1e6;
        last = now;
        total_ms += cpu_ms;
        if (frame_ms > worst)
            worst = frame_ms;
        frames++;
        if (armTicksToNs(now - second_start) >= 1000000000ull) {
            shown_ms = total_ms / frames;
            logf_("second %d: %d frames, drawing %.2f ms a frame, slowest frame %.2f ms\n", ++seconds, frames,
                  shown_ms, worst);
            frames = 0, worst = 0.0, total_ms = 0.0;
            second_start = now;
        }
    }

    logf_("exit: freeing GL objects\n");
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &cover_texture);
    glDeleteTextures(1, &background_texture);
    glDeleteProgram(program);
    logf_("exit: closing EGL\n");
    eglMakeCurrent(window.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(window.display, window.context);
    eglDestroySurface(window.display, window.surface);
    eglTerminate(window.display);
    logf_("exit: done, returning from main\n");
    fclose(g_log);
    return 0;
}
