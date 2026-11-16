// Now Playing's cassette. The still parts (shell, label, window, the head
// opening, screws) are drawn once per song and kept as an image of that part
// of the screen; each frame copies it back and draws only the turning reels,
// clipped to the window, then the bits of label around the window's round ends.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_cassette.h"

#define TAU 6.2831853f
#define TAPE_SPEED 26.0f  // Layout pixels of tape a second, for how fast the reels turn.
#define MARGIN_FB 30      // Framebuffer pixels kept around the cassette (its shadow).

#define SHELL_DARK GFX_RGB(0x18, 0x18, 0x1D)
#define SHELL GFX_RGB(0x2A, 0x2A, 0x31)
#define SHELL_EDGE GFX_RGB(0x4C, 0x4C, 0x57)
#define PAPER GFX_RGB(0xF3, 0xED, 0xDF)
#define RULE GFX_RGB(0xD9, 0xD1, 0xBF)
#define PEN GFX_RGB(0x26, 0x34, 0x5E)
#define FRAME GFX_RGB(0xB9, 0xB1, 0xA0)
#define GLASS GFX_RGB(0x13, 0x13, 0x17)
#define TAPE GFX_RGB(0x3B, 0x29, 0x1E)
#define TAPE_SHINE GFX_RGB(0x5A, 0x41, 0x31)
#define HUB GFX_RGB(0xF2, 0xF0, 0xEA)
#define HOLE GFX_RGB(0x0D, 0x0D, 0x10)

typedef struct {
    int x, y, w, h;          // The cassette, layout units.
    int wx, wy, ww, wh;      // Its window.
} Layout;

// The saved still image, and what it showed.
typedef struct {
    uint32_t *pixels;
    int fx, fy, fw, fh;      // Framebuffer rectangle.
    uint32_t key;
    uint32_t *corners[4];    // The label at the window's four round corners, with alpha.
    int corner_fb;
} Still;

static Still g_still;

static Layout layoutFor(int x, int y, int size)
{
    Layout l = {x, 0, size, size * 63 / 100, 0, 0, 0, 0};
    l.y = y + (size - l.h) / 2 - 8;
    l.ww = l.w / 2;
    l.wh = 58;
    l.wx = l.x + (l.w - l.ww) / 2;
    l.wy = l.y + 14 + 70;
    return l;
}

static uint32_t hashText(uint32_t h, const char *s)
{
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h * 16777619u;
}

static Color inkOn(Color band)
{
    unsigned luma = ((band & 0xFF) * 3 + (band >> 8 & 0xFF) * 6 + (band >> 16 & 0xFF)) / 10;
    return luma > 150 ? GFX_RGB(0x22, 0x22, 0x26) : GFX_RGB(0xFF, 0xFF, 0xFF);
}

static void screw(float cx, float cy)
{
    gfxArc(cx, cy, 2.75f, 5.5f, 0.0f, TAU, GFX_RGB(0x8C, 0x8C, 0x95));
    gfxLine(cx - 3.0f, cy - 3.0f, cx + 3.0f, cy + 3.0f, 1.3f, GFX_RGB(0x3A, 0x3A, 0x40));
    gfxLine(cx - 3.0f, cy + 3.0f, cx + 3.0f, cy - 3.0f, 1.3f, GFX_RGB(0x3A, 0x3A, 0x40));
}

static void drawShell(const Layout *l)
{
    for (int i = 0; i < 6; i++)  // A soft shadow below.
        gfxBlendRect(l->x + 10 - i, l->y + 14 - i, l->w - 20 + 2 * i, l->h - 8 + 2 * i, GFX_RGB(0, 0, 0), 16);
    gfxFillRoundRect(l->x, l->y, l->w, l->h, 18, SHELL_DARK);
    gfxFillRoundRect(l->x + 2, l->y + 2, l->w - 4, l->h - 4, 16, SHELL);
    gfxBlendRect(l->x + 18, l->y + 2, l->w - 36, l->h / 2, GFX_RGB(0xFF, 0xFF, 0xFF), 7);  // Light from above.
    gfxLine(l->x + 20.0f, l->y + 3.0f, l->x + l->w - 20.0f, l->y + 3.0f, 1.5f, SHELL_EDGE);
    gfxFillRect(l->x + 34, l->y, 20, 4, HOLE);            // The record-protection tabs.
    gfxFillRect(l->x + l->w - 54, l->y, 20, 4, HOLE);
}

static void drawLabel(const Layout *l, const UiTrack *track, Color band, double duration)
{
    int lx = l->x + 16, ly = l->y + 14, lw = l->w - 32, lh = 154;
    gfxFillRoundRect(lx, ly, lw, lh, 8, PAPER);
    gfxFillRoundRect(lx, ly, lw, 34, 8, band);
    gfxFillRect(lx, ly + 20, lw, 14, band);
    gfxFillRect(lx, ly + 34, lw, 4, gfxMix(band, GFX_RGB(0, 0, 0), 70));
    gfxFillRect(lx, ly + 38, lw, 2, gfxMix(band, PAPER, 140));
    gfxFillRoundRect(lx + 10, ly + 6, 22, 22, 5, PAPER);
    fontDrawCentered(FONT_BOLD, 16, lx + 21.0f, ly + 17.0f, "A", gfxMix(band, GFX_RGB(0, 0, 0), 110));
    fontDrawClipped(FONT_BOLD, 18, lx + 42, ly + 6, lw - 54, track->title, inkOn(band));

    // Written on the lines, in pen: the artist above the window, the album below.
    gfxFillRect(lx + 12, ly + 66, lw - 24, 1, RULE);
    gfxFillRect(lx + 12, ly + lh - 8, lw - 24, 1, RULE);
    if (track->artist[0])
        fontDrawClipped(FONT_REGULAR, 16, lx + 14, ly + 45, lw - 28, track->artist, PEN);
    if (track->album[0])
        fontDrawClipped(FONT_REGULAR, 15, lx + 14, ly + lh - 26, lw - 28, track->album, PEN);

    // The window: a printed frame, then the glass.
    gfxFillRoundRect(l->wx - 6, l->wy - 6, l->ww + 12, l->wh + 12, (l->wh + 12) / 2, FRAME);
    gfxFillRoundRect(l->wx - 2, l->wy - 2, l->ww + 4, l->wh + 4, (l->wh + 4) / 2, gfxMix(FRAME, HOLE, 120));
    gfxFillRoundRect(l->wx, l->wy, l->ww, l->wh, l->wh / 2, GLASS);

    // Beside it: the cover on the left, the length and STEREO on the right.
    int side = l->wx - 6 - lx;
    if (track->cover) {
        int thumb = 46, ty = l->wy + (l->wh - thumb) / 2;
        gfxBlendRect(lx + (side - thumb) / 2 + 2, ty + 2, thumb, thumb, GFX_RGB(0, 0, 0), 60);
        uiDrawCover(lx + (side - thumb) / 2, ty, thumb, track->cover);
    }
    char length[16] = "";
    if (duration > 0)
        utilFormatTime(length, sizeof(length), duration);
    float right_cx = (l->wx + l->ww + 6 + lx + lw) / 2.0f;
    if (length[0])
        fontDrawCentered(FONT_BOLD, 20, right_cx, l->wy + 20.0f, length, PEN);
    fontDrawCentered(FONT_BOLD, 11, right_cx, l->wy + 42.0f, "STEREO", gfxMix(PEN, PAPER, 90));
}

// The raised strip at the bottom, where the heads meet the tape.
static void drawHeadArea(const Layout *l)
{
    float top = l->y + l->h - 52.0f, bottom = l->y + l->h - 2.0f;
    float tl = l->x + l->w * 0.21f, tr = l->x + l->w * 0.79f, bl = l->x + l->w * 0.13f, br = l->x + l->w * 0.87f;
    Color plate = GFX_RGB(0x22, 0x22, 0x28);
    gfxTriangle(tl, top, bl, bottom, tl, bottom, plate);
    gfxTriangle(tr, top, tr, bottom, br, bottom, plate);
    gfxFillRect((int)tl, (int)top, (int)(tr - tl) + 1, (int)(bottom - top), plate);
    gfxLine(tl, top, tr, top, 1.5f, SHELL_EDGE);
    gfxLine(tl, top, bl, bottom, 1.2f, SHELL_EDGE);
    gfxLine(tr, top, br, bottom, 1.2f, SHELL_EDGE);

    float cx = l->x + l->w / 2.0f, hy = bottom - 20.0f;
    gfxFillRoundRect((int)(cx - 30), (int)(hy - 9), 60, 18, 3, HOLE);           // The head opening,
    gfxFillRect((int)(cx - 30), (int)(hy + 3), 60, 4, TAPE);                    // the tape in it,
    gfxFillRoundRect((int)(cx - 10), (int)(hy - 8), 20, 9, 2, GFX_RGB(0x9A, 0x7E, 0x66));  // its felt pad,
    for (int side = -1; side <= 1; side += 2) {                                 // capstan and guide holes.
        gfxArc(cx + side * 62.0f, hy, 3.5f, 7.0f, 0.0f, TAU, HOLE);
        gfxFillRoundRect((int)(cx + side * 96.0f - 6), (int)(hy - 6), 12, 12, 2, HOLE);
        gfxFillRect((int)(cx + side * 96.0f - 6), (int)(hy + 3), 12, 3, TAPE);
    }
    screw(cx, top - 10.0f);
}

static void drawStill(const Layout *l, const UiTrack *track, Color band, double duration)
{
    drawShell(l);
    drawLabel(l, track, band, duration);
    drawHeadArea(l);
    screw(l->x + 9.0f, l->y + 9.0f);
    screw(l->x + l->w - 9.0f, l->y + 9.0f);
    screw(l->x + 12.0f, l->y + l->h - 12.0f);
    screw(l->x + l->w - 12.0f, l->y + l->h - 12.0f);
}

// Saves the label at the window's round corners: each pixel's alpha is how
// much of it lies outside the window, so drawing them over the reels trims
// the reels to the window's shape.
static bool saveCorners(const Layout *l)
{
    int r = gfxToFb(l->wh / 2), wx = gfxToFb(l->wx), wy = gfxToFb(l->wy);
    int wx1 = gfxToFb(l->wx + l->ww), wy1 = gfxToFb(l->wy + l->wh);
    for (int i = 0; i < 4; i++) {
        free(g_still.corners[i]);
        g_still.corners[i] = malloc((size_t)r * r * sizeof(uint32_t));
        if (!g_still.corners[i])
            return false;
        int ox = i & 1 ? wx1 - r : wx, oy = i & 2 ? wy1 - r : wy;
        float ccx = i & 1 ? 0.0f : (float)r, ccy = i & 2 ? 0.0f : (float)r;  // The arc's center in the square.
        for (int y = 0; y < r; y++) {
            for (int x = 0; x < r; x++) {
                float dx = x + 0.5f - ccx, dy = y + 0.5f - ccy, d = sqrtf(dx * dx + dy * dy);
                float outside = d - r + 0.5f;
                outside = outside < 0.0f ? 0.0f : outside > 1.0f ? 1.0f : outside;
                int sx = ox + x - g_still.fx, sy = oy + y - g_still.fy;
                uint32_t p = g_still.pixels[(size_t)sy * g_still.fw + sx] & 0xFFFFFFu;
                g_still.corners[i][(size_t)y * r + x] = p | (uint32_t)(outside * 255.0f) << 24;
            }
        }
    }
    g_still.corner_fb = r;
    return true;
}

static bool prepareStill(const Layout *l, const UiTrack *track, Color band, double duration)
{
    uint32_t key = hashText(2166136261u, track->title);
    key = hashText(key, track->artist);
    key = hashText(key, track->album);
    key ^= (uint32_t)(uintptr_t)track->cover * 2654435761u ^ (uint32_t)(uintptr_t)backgroundPixels() ^ band ^
           (uint32_t)(duration * 10.0) * 40503u ^ (uint32_t)(l->x * 31 + l->y);
    int fx = gfxToFb(l->x) - MARGIN_FB, fy = gfxToFb(l->y) - MARGIN_FB;
    int fw = gfxToFb(l->w) + 2 * MARGIN_FB, fh = gfxToFb(l->h) + 2 * MARGIN_FB;
    if (g_still.pixels && g_still.key == key && g_still.fx == fx && g_still.fy == fy)
        return true;
    if (!g_still.pixels || g_still.fw != fw || g_still.fh != fh) {
        free(g_still.pixels);
        g_still.pixels = malloc((size_t)fw * fh * sizeof(uint32_t));
        if (!g_still.pixels)
            return false;
    }
    g_still.fx = fx, g_still.fy = fy, g_still.fw = fw, g_still.fh = fh;
    drawStill(l, track, band, duration);  // Onto the screen, over what's behind, then kept.
    gfxSaveRectFb(fx, fy, fw, fh, g_still.pixels);
    g_still.key = key;
    if (!saveCorners(l)) {
        g_still.key = 0;
        return false;
    }
    return true;
}

// One reel: the wound tape, with a little shine, and the white hub with the
// teeth that grip the spindle.
static void drawReel(float cx, float cy, float tape, float angle)
{
    gfxArc(cx, cy, tape / 2.0f, tape, 0.0f, TAU, TAPE);
    if (tape > 24.0f)
        gfxArc(cx, cy, tape * 0.72f, 1.5f, 0.0f, TAU, TAPE_SHINE);
    gfxArc(cx, cy, tape - 1.0f, 1.2f, 0.0f, TAU, TAPE_SHINE);
    gfxArc(cx, cy, 12.5f, 7.0f, 0.0f, TAU, HUB);
    gfxArc(cx, cy, 4.5f, 9.0f, 0.0f, TAU, HOLE);
    for (int i = 0; i < 6; i++) {
        float a = angle + i * TAU / 6.0f;
        gfxLine(cx + cosf(a) * 9.5f, cy + sinf(a) * 9.5f, cx + cosf(a) * 5.5f, cy + sinf(a) * 5.5f, 3.0f, HUB);
    }
}

void uiDrawCassette(int x, int y, int size, const UiTrack *track, double spin, double position, double duration)
{
    Layout l = layoutFor(x, y, size);
    Color band = track->cover ? (track->cover->vibrant ? track->cover->vibrant : track->cover->average)
                              : COLOR_ACCENT;
    bool kept = gfxCanReadBack();  // The GPU draws the cassette whole every frame instead.
    if (!kept || !prepareStill(&l, track, band, duration)) {
        drawStill(&l, track, band, duration);  // Out of memory: draw it all every frame.
    } else {
        gfxDrawImageFb(g_still.fx, g_still.fy, g_still.fw, g_still.fh, g_still.pixels);
    }

    // The tape winds from the left reel to the right one over the song.
    double done = duration > 0 ? position / duration : 0.0;
    float f = (float)(done < 0.0 ? 0.0 : done > 1.0 ? 1.0 : done);
    float empty = 18.0f, full = l.w * 0.13f;
    float left_r = sqrtf(empty * empty + (1.0f - f) * (full * full - empty * empty));
    float right_r = sqrtf(empty * empty + f * (full * full - empty * empty));
    float cy = l.wy + l.wh / 2.0f, lx = l.x + l.w * 0.335f, rx = l.x + l.w * 0.665f;
    gfxSetClipRound(l.wx, l.wy, l.ww, l.wh, l.wh / 2);
    drawReel(lx, cy, left_r, (float)fmod(spin * TAPE_SPEED / left_r, TAU));
    drawReel(rx, cy, right_r, (float)fmod(spin * TAPE_SPEED / right_r, TAU));
    gfxResetClip();
    if (kept && g_still.corner_fb) {
        int r = g_still.corner_fb;
        int wx = gfxToFb(l.wx), wy = gfxToFb(l.wy), wx1 = gfxToFb(l.wx + l.ww), wy1 = gfxToFb(l.wy + l.wh);
        for (int i = 0; i < 4; i++)
            gfxDrawImageFb(i & 1 ? wx1 - r : wx, i & 2 ? wy1 - r : wy, r, r, g_still.corners[i]);
    }
    // A reflection on the glass, over the reels.
    gfxBlendRect(l.wx + l.wh / 2, l.wy + 4, l.ww - l.wh, 2, GFX_RGB(0xFF, 0xFF, 0xFF), 22);
    gfxBlendRect(l.wx + l.wh / 2 + 12, l.wy + 8, l.ww / 4, 1, GFX_RGB(0xFF, 0xFF, 0xFF), 14);
}
