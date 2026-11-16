// Now Playing's record, CD and cassette. What doesn't turn (the record's
// grooves and shine, the CD's plastic) is drawn once into an image; each
// frame only turns the cover on top. They turn only while a song plays.
#include <math.h>
#include <stdlib.h>

#include "nowstyle.h"
#include "ui_common.h"
#include "ui_nowstyle.h"

#define TAU 6.2831853f
#define VINYL_RPS 0.25f  // A turn every 4 s: calmer on screen than a real 33 1/3 rpm.
#define CD_RPS 0.2f
#define TAPE_SPEED 26.0f                     // Layout pixels of tape per second, for the reels.
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

// One reel in the cassette's window: the wound tape and the turning hub.
static void drawReel(float cx, float cy, float tape_radius, float angle)
{
    gfxArc(cx, cy, tape_radius / 2.0f, tape_radius, 0.0f, TAU, GFX_RGB(0x4A, 0x33, 0x26));
    gfxArc(cx, cy, 8.5f, 17.0f, 0.0f, TAU, GFX_RGB(0xEE, 0xEE, 0xF0));
    gfxArc(cx, cy, 5.0f, 10.0f, 0.0f, TAU, GFX_RGB(0x26, 0x26, 0x2A));
    for (int i = 0; i < 6; i++) {  // The teeth that grip the spindle.
        float a = angle + i * TAU / 6.0f;
        gfxLine(cx + cosf(a) * 7.0f, cy + sinf(a) * 7.0f, cx + cosf(a) * 12.0f, cy + sinf(a) * 12.0f, 3.0f,
                GFX_RGB(0xEE, 0xEE, 0xF0));
    }
}

static void drawCassette(int x, int y, int size, const UiTrack *track, double spin, double progress)
{
    int w = size, h = size * 64 / 100;
    y += (size - h) / 2;
    Color shell = GFX_RGB(0x2A, 0x2A, 0x30), paper = GFX_RGB(0xEE, 0xE8, 0xDA);
    Color band = track->cover ? (track->cover->vibrant ? track->cover->vibrant : track->cover->average)
                              : COLOR_ACCENT;
    gfxFillRoundRect(x, y, w, h, 16, shell);

    // The reels first; the label is drawn around the window, so it hides
    // the tape that wouldn't show.
    int win_x = x + w * 28 / 100, win_y = y + h * 42 / 100, win_w = w * 44 / 100, win_h = h * 26 / 100;
    float left_x = x + w * 0.33f, right_x = x + w * 0.67f, reel_y = win_y + win_h / 2.0f;
    float empty = 19.0f, full = w * 0.14f;
    float done = (float)(progress < 0.0 ? 0.0 : progress > 1.0 ? 1.0 : progress);
    float left_r = sqrtf(empty * empty + (1.0f - done) * (full * full - empty * empty));
    float right_r = sqrtf(empty * empty + done * (full * full - empty * empty));
    gfxFillRect(win_x, win_y, win_w, win_h, GFX_RGB(0x14, 0x14, 0x18));
    drawReel(left_x, reel_y, left_r, (float)fmod(spin * TAPE_SPEED / left_r, TAU));
    drawReel(right_x, reel_y, right_r, (float)fmod(spin * TAPE_SPEED / right_r, TAU));

    int lx = x + 18, ly = y + 16, lw = w - 36, lh = h * 64 / 100;
    gfxFillRect(x + 40, ly + lh, w - 80, y + h - 8 - ly - lh, shell);  // Hides a full reel below the label.
    gfxFillRect(lx, ly, lw, win_y - ly, paper);                              // Above the window.
    gfxFillRect(lx, win_y + win_h, lw, ly + lh - win_y - win_h, paper);      // Below it.
    gfxFillRect(lx, win_y, win_x - lx, win_h, paper);                        // Left of it.
    gfxFillRect(win_x + win_w, win_y, lx + lw - win_x - win_w, win_h, paper); // Right of it.
    gfxFillRect(lx, ly, lw, 30, band);
    unsigned luma = ((band & 0xFF) * 3 + (band >> 8 & 0xFF) * 6 + (band >> 16 & 0xFF)) / 10;
    Color ink = luma > 150 ? GFX_RGB(0x22, 0x22, 0x26) : GFX_RGB(0xFF, 0xFF, 0xFF);  // Readable on the band.
    fontDraw(FONT_BOLD, 18, lx + 12, ly + 5, "A", ink);
    fontDrawClipped(FONT_BOLD, 18, lx + 36, ly + 5, lw - 48, track->title, ink);
    if (track->artist[0])
        fontDrawClipped(FONT_REGULAR, 16, lx + 12, ly + 38, lw - 24, track->artist, GFX_RGB(0x3A, 0x3A, 0x40));
    if (track->cover) {  // The cover, small, on the label's right.
        int thumb = win_h + 8;
        uiDrawCover(win_x + win_w + (lx + lw - win_x - win_w - thumb) / 2, win_y - 4, thumb, track->cover);
    }
    gfxFillRoundRect(win_x - 4, win_y - 4, win_w + 8, 4, 2, GFX_RGB(0x9A, 0x96, 0x8C));  // The window's frame.
    gfxFillRoundRect(win_x - 4, win_y + win_h, win_w + 8, 4, 2, GFX_RGB(0x9A, 0x96, 0x8C));

    // The bottom edge, where the tape shows, with its holes and the screws.
    int bx = x + w * 18 / 100, by = y + h - h * 22 / 100, bw = w * 64 / 100;
    gfxFillRoundRect(bx, by, bw, h * 22 / 100 - 6, 10, GFX_RGB(0x22, 0x22, 0x27));
    for (int i = 0; i < 4; i++)
        gfxArc(bx + bw * (0.18f + 0.213f * i), by + h * 0.10f, 3.0f, 6.0f, 0.0f, TAU, GFX_RGB(0x0E, 0x0E, 0x10));
    float screws[5][2] = {{x + 12.0f, y + 12.0f}, {x + w - 12.0f, y + 12.0f}, {x + 12.0f, y + h - 12.0f},
                          {x + w - 12.0f, y + h - 12.0f}, {x + w / 2.0f, y + h - 12.0f}};
    for (int i = 0; i < 5; i++)
        gfxArc(screws[i][0], screws[i][1], 2.5f, 5.0f, 0.0f, TAU, GFX_RGB(0x6A, 0x6A, 0x72));
}

bool uiDrawNowStyle(int style, int x, int y, int size, const UiTrack *track, bool playing, uint64_t now_ns,
                    double progress)
{
    if (style == NOW_STYLE_COVER)
        return false;
    double spin = uiNowStyleSpin(playing, now_ns);
    if (style == NOW_STYLE_VINYL)
        drawVinyl(x, y, size, track, spin);
    else if (style == NOW_STYLE_CD)
        drawCd(x, y, size, track, spin);
    else
        drawCassette(x, y, size, track, spin, progress);
    return true;
}
