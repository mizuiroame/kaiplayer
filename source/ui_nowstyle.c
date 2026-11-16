// Now Playing's record and CD (the cassette is in ui_cassette.c). What
// doesn't turn (the record's grooves and shine, the CD's plastic) is drawn
// once into an image; each frame only turns the cover on top. They turn only
// while a song plays.
#include <math.h>
#include <stdlib.h>

#include "nowstyle.h"
#include "ui_common.h"
#include "ui_cassette.h"
#include "ui_nowstyle.h"

#define TAU 6.2831853f
#define VINYL_RPS 0.25f  // A turn every 4 s: calmer on screen than a real 33 1/3 rpm.
#define CD_RPS 0.2f
#define MAX_FRAME_SECONDS 0.25               // After a pause or another view, don't jump.

#define VINYL_LABEL 0.34f  // Radii as fractions of the disc's.
#define CD_PRINT_OUTER 0.97f
#define CD_PRINT_INNER 0.22f
#define CD_HOLE 0.075f

typedef struct {
    int size;  // Framebuffer pixels, square.
    uint32_t *pixels;
} Layer;

static Layer g_vinyl, g_cd_base, g_cd_gloss;
static double g_spin_seconds;
static uint64_t g_last_ns;

double uiNowStyleSpin(bool playing, uint64_t now_ns)
{
    if (playing && g_last_ns && now_ns > g_last_ns) {
        double step = (double)(now_ns - g_last_ns) / 1e9;
        g_spin_seconds += step > MAX_FRAME_SECONDS ? MAX_FRAME_SECONDS : step;
    }
    g_last_ns = now_ns;
    return g_spin_seconds;
}

static uint32_t rgba(float r, float g, float b, float a)
{
    #define CH(v) ((uint32_t)((v) < 0.0f ? 0.0f : (v) > 255.0f ? 255.0f : (v)))
    return CH(r) | CH(g) << 8 | CH(b) << 16 | CH(a) << 24;
    #undef CH
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

// Fills a layer for a disc of the given layout radius, one pixel at a time:
// shade gets the distance from the center (0-1 of the radius), the angle
// and the edge coverage, and returns the pixel.
typedef uint32_t (*Shade)(float r, float angle, float cover);

static bool makeLayer(Layer *layer, int radius, Shade shade)
{
    int size = gfxToFb(radius * 2) + 2;
    if (layer->pixels && layer->size == size)
        return true;
    free(layer->pixels);
    layer->pixels = malloc((size_t)size * size * sizeof(uint32_t));
    layer->size = layer->pixels ? size : 0;
    if (!layer->pixels)
        return false;
    gfxImageChanged(layer->pixels);
    float c = size / 2.0f, rad = gfxToFb(radius);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c, d = sqrtf(dx * dx + dy * dy);
            float cover = clamp01(rad - d + 0.5f);
            layer->pixels[(size_t)y * size + x] = cover > 0.0f ? shade(d / rad, atan2f(dy, dx), cover) : 0;
        }
    }
    return true;
}

static void drawLayer(const Layer *layer, int cx, int cy)
{
    gfxDrawImageFb(gfxToFb(cx) - layer->size / 2, gfxToFb(cy) - layer->size / 2, layer->size, layer->size,
                   layer->pixels);
}

// Black vinyl: fine grooves, darker gaps between the songs, and two
// wedges of light where the room's lamps would reflect.
static uint32_t shadeVinyl(float r, float angle, float cover)
{
    float v = 22.0f + 5.0f * sinf(r * 900.0f);
    static const float GAPS[] = {0.52f, 0.63f, 0.74f, 0.86f};
    for (int i = 0; i < 4; i++)
        if (fabsf(r - GAPS[i]) < 0.006f)
            v = 12.0f;
    if (r > 0.975f)
        v = 34.0f;  // The rim.
    float shine = powf(fabsf(cosf(angle + 0.7f)), 18.0f) * 36.0f * clamp01((r - VINYL_LABEL) * 3.0f);
    v += shine;
    return rgba(v, v, v + 2.0f, 255.0f * cover);
}

// A CD under its print: the clear plastic around the hole and at the rim.
static uint32_t shadeCdBase(float r, float angle, float cover)
{
    (void)angle;
    if (r < CD_HOLE)
        return 0;
    float hole_edge = clamp01((r - CD_HOLE) * 400.0f);
    if (r < 0.16f)
        return rgba(205.0f, 208.0f, 214.0f, 120.0f * hole_edge * cover);  // Clear center.
    if (r < CD_PRINT_INNER)
        return rgba(186.0f, 190.0f, 198.0f, 235.0f * cover);  // The stacking ring.
    if (r < CD_PRINT_OUTER - 0.01f)
        return 0;  // Under the print, which is drawn over it every frame.
    return rgba(214.0f, 218.0f, 226.0f, 200.0f * cover);
}

// The gloss over a CD's print: soft light in two opposite wedges.
static uint32_t shadeCdGloss(float r, float angle, float cover)
{
    if (r < CD_PRINT_INNER || r > CD_PRINT_OUTER)
        return 0;
    float a = powf(fabsf(cosf(angle - 0.9f)), 10.0f) * 70.0f;
    return rgba(255.0f, 255.0f, 255.0f, a * cover);
}

// What turns when there's no cover: a plain label with a mark on it.
static void drawPlainLabel(float cx, float cy, float radius, float hole, float angle)
{
    gfxArc(cx, cy, (radius + hole) / 2.0f, radius - hole, 0.0f, TAU, gfxMix(COLOR_SURFACE_HI, COLOR_ACCENT, 160));
    float mid = (radius + hole) / 2.0f;
    gfxArc(cx, cy, mid, (radius - hole) * 0.25f, angle, angle + 0.9f, gfxMix(COLOR_ACCENT, COLOR_TEXT, 110));
}

static void drawVinyl(int x, int y, int size, const UiTrack *track, double spin)
{
    int r = size / 2, cx = x + r, cy = y + r;
    float angle = (float)fmod(spin * VINYL_RPS * TAU, TAU);
    if (makeLayer(&g_vinyl, r, shadeVinyl))
        drawLayer(&g_vinyl, cx, cy);
    if (track->cover)
        gfxBlitDisc((float)cx, (float)cy, r * VINYL_LABEL, 0.0f, track->cover->size, track->cover->pixels, angle);
    else
        drawPlainLabel((float)cx, (float)cy, r * VINYL_LABEL, 0.0f, angle);
    gfxArc((float)cx, (float)cy, 3.0f, 6.0f, 0.0f, TAU, GFX_RGB(0xD8, 0xD8, 0xDC));  // The spindle.

    // The tonearm, resting on the record from its post at the top right.
    float px = cx + r * 1.02f, py = cy - r * 0.78f, hx = cx + r * 0.50f, hy = cy + r * 0.30f;
    gfxArc(px, py, 9.0f, 18.0f, 0.0f, TAU, GFX_RGB(0x3A, 0x3A, 0x40));
    gfxLine(px, py, hx, hy, 6.0f, GFX_RGB(0xC8, 0xC8, 0xCE));
    gfxLine(hx, hy, hx - 14.0f, hy + 12.0f, 13.0f, GFX_RGB(0x8E, 0x8E, 0x96));
    gfxArc(px, py, 4.0f, 8.0f, 0.0f, TAU, GFX_RGB(0x9A, 0x9A, 0xA2));
}

static void drawCd(int x, int y, int size, const UiTrack *track, double spin)
{
    int r = size / 2, cx = x + r, cy = y + r;
    float angle = (float)fmod(spin * CD_RPS * TAU, TAU);
    if (makeLayer(&g_cd_base, r, shadeCdBase))
        drawLayer(&g_cd_base, cx, cy);
    if (track->cover)
        gfxBlitDisc((float)cx, (float)cy, r * CD_PRINT_OUTER, r * CD_PRINT_INNER, track->cover->size,
                    track->cover->pixels, angle);
    else
        drawPlainLabel((float)cx, (float)cy, r * CD_PRINT_OUTER, r * CD_PRINT_INNER, angle);
    if (makeLayer(&g_cd_gloss, r, shadeCdGloss))
        drawLayer(&g_cd_gloss, cx, cy);
}

bool uiDrawNowStyle(int style, int x, int y, int size, const UiTrack *track, bool playing, uint64_t now_ns,
                    double position, double duration)
{
    if (style == NOW_STYLE_COVER)
        return false;
    double spin = uiNowStyleSpin(playing, now_ns);
    if (style == NOW_STYLE_VINYL)
        drawVinyl(x, y, size, track, spin);
    else if (style == NOW_STYLE_CD)
        drawCd(x, y, size, track, spin);
    else
        uiDrawCassette(x, y, size, track, spin, position, duration);
    return true;
}
