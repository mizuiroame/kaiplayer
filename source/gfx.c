#include "gfx.h"

#include <math.h>
#include <string.h>

static uint32_t *g_pixels;
static uint32_t g_stride;  // In pixels.

void gfxSetTarget(uint32_t *pixels, uint32_t stride)
{
    g_pixels = pixels;
    g_stride = stride;
}

Color gfxMix(Color a, Color b, unsigned t)
{
    if (t > 255)
        t = 255;
    unsigned inv = 255 - t;
    unsigned r = ((a & 0xFF) * inv + (b & 0xFF) * t) / 255;
    unsigned g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * t) / 255;
    unsigned bl = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * t) / 255;
    return GFX_RGB(r, g, bl);
}

// Clips a rectangle to the screen. False if nothing is left.
static bool clip(int *x, int *y, int *w, int *h)
{
    int x0 = *x < 0 ? 0 : *x, y0 = *y < 0 ? 0 : *y;
    int x1 = *x + *w > GFX_WIDTH ? GFX_WIDTH : *x + *w;
    int y1 = *y + *h > GFX_HEIGHT ? GFX_HEIGHT : *y + *h;
    if (!g_pixels || x1 <= x0 || y1 <= y0)
        return false;
    *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
    return true;
}

static inline void blendPixel(uint32_t *p, Color color, unsigned alpha)
{
    if (alpha >= 255)
        *p = color;
    else if (alpha > 0)
        *p = gfxMix(*p, color, alpha);
}

void gfxClear(Color color)
{
    gfxFillRect(0, 0, GFX_WIDTH, GFX_HEIGHT, color);
}

void gfxFillRect(int x, int y, int w, int h, Color color)
{
    if (!clip(&x, &y, &w, &h))
        return;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride + x;
        for (int i = 0; i < w; i++)
            row[i] = color;
    }
}

void gfxBlendRect(int x, int y, int w, int h, Color color, unsigned alpha)
{
    if (!clip(&x, &y, &w, &h))
        return;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride + x;
        for (int i = 0; i < w; i++)
            blendPixel(&row[i], color, alpha);
    }
}

// Coverage (0-255) of pixel (px, py) by a circle, with a 1-pixel soft edge.
static unsigned circleCoverage(float px, float py, float cx, float cy, float r)
{
    float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
    float c = r - d + 0.5f;
    if (c <= 0.0f)
        return 0;
    return c >= 1.0f ? 255 : (unsigned)(c * 255.0f);
}

void gfxFillRoundRect(int x, int y, int w, int h, int radius, Color color)
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    gfxFillRect(x, y + radius, w, h - 2 * radius, color);
    gfxFillRect(x + radius, y, w - 2 * radius, radius, color);
    gfxFillRect(x + radius, y + h - radius, w - 2 * radius, radius, color);
    for (int dy = 0; dy < radius; dy++) {
        for (int dx = 0; dx < radius; dx++) {
            unsigned a = circleCoverage(dx + 0.5f, dy + 0.5f, (float)radius, (float)radius, (float)radius);
            if (a == 0)
                continue;
            int xs[2] = {x + dx, x + w - 1 - dx}, ys[2] = {y + dy, y + h - 1 - dy};
            for (int i = 0; i < 4; i++) {
                int px = xs[i & 1], py = ys[i >> 1];
                if (g_pixels && px >= 0 && px < GFX_WIDTH && py >= 0 && py < GFX_HEIGHT)
                    blendPixel(g_pixels + (size_t)py * g_stride + px, color, a);
            }
        }
    }
}

void gfxFillCircle(int cx, int cy, int radius, Color color)
{
    int x = cx - radius - 1, y = cy - radius - 1, w = radius * 2 + 3, h = w;
    if (!clip(&x, &y, &w, &h))
        return;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride;
        for (int px = x; px < x + w; px++)
            blendPixel(&row[px], color, circleCoverage(px + 0.5f, py + 0.5f, (float)cx, (float)cy, (float)radius));
    }
}

void gfxGradientV(int x, int y, int w, int h, Color top, Color bottom)
{
    for (int i = 0; i < h; i++)
        gfxFillRect(x, y + i, w, 1, gfxMix(top, bottom, h > 1 ? (unsigned)(i * 255 / (h - 1)) : 0));
}

#define TRIANGLE_SAMPLES 4  // Per axis: 16 samples per pixel for smooth edges.

// An isosceles triangle in a w x h box pointing right (or left), with
// anti-aliased edges from supersampling.
static void fillTriangle(int x, int y, int w, int h, Color color, bool left)
{
    int bx = x, by = y, bw = w, bh = h;
    if (w <= 0 || h <= 0 || !clip(&bx, &by, &bw, &bh))
        return;
    float half = (float)h / 2.0f;
    for (int py = by; py < by + bh; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride;
        for (int px = bx; px < bx + bw; px++) {
            unsigned inside = 0;
            for (int sy = 0; sy < TRIANGLE_SAMPLES; sy++) {
                float fy = (float)(py - y) + (sy + 0.5f) / TRIANGLE_SAMPLES;
                float reach = (float)w * (1.0f - fabsf(fy - half) / half);  // Width at this height.
                for (int sx = 0; sx < TRIANGLE_SAMPLES; sx++) {
                    float fx = (float)(px - x) + (sx + 0.5f) / TRIANGLE_SAMPLES;
                    inside += (left ? (float)w - fx : fx) < reach;
                }
            }
            blendPixel(&row[px], color, inside * 255 / (TRIANGLE_SAMPLES * TRIANGLE_SAMPLES));
        }
    }
}

void gfxFillTriangleRight(int x, int y, int w, int h, Color color)
{
    fillTriangle(x, y, w, h, color, false);
}

void gfxFillTriangleLeft(int x, int y, int w, int h, Color color)
{
    fillTriangle(x, y, w, h, color, true);
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// Calls shade on every pixel of the box around a shape, blending color by
// the coverage it returns (0-1).
typedef float (*Coverage)(float px, float py, const float *shape);

static void fillShape(float x0, float y0, float x1, float y1, const float *shape, Coverage coverage, Color color)
{
    int x = (int)floorf(x0) - 1, y = (int)floorf(y0) - 1;
    int w = (int)ceilf(x1) + 2 - x, h = (int)ceilf(y1) + 2 - y;
    if (!clip(&x, &y, &w, &h))
        return;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride;
        for (int px = x; px < x + w; px++)
            blendPixel(&row[px], color, (unsigned)(coverage(px + 0.5f, py + 0.5f, shape) * 255.0f));
    }
}

// shape: x0, y0, x1, y1, half thickness.
static float lineCoverage(float px, float py, const float *s)
{
    float dx = s[2] - s[0], dy = s[3] - s[1], len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? clampf(((px - s[0]) * dx + (py - s[1]) * dy) / len2, 0.0f, 1.0f) : 0.0f;
    float ex = px - (s[0] + t * dx), ey = py - (s[1] + t * dy);
    return clampf(s[4] - sqrtf(ex * ex + ey * ey) + 0.5f, 0.0f, 1.0f);
}

void gfxLine(float x0, float y0, float x1, float y1, float thickness, Color color)
{
    float r = thickness / 2.0f, s[5] = {x0, y0, x1, y1, r};
    fillShape(fminf(x0, x1) - r, fminf(y0, y1) - r, fmaxf(x0, x1) + r, fmaxf(y0, y1) + r, s, lineCoverage, color);
}

// shape: cx, cy, radius, half thickness, start, sweep (0 to 2 pi).
static float arcCoverage(float px, float py, const float *s)
{
    float dx = px - s[0], dy = py - s[1];
    float angle = atan2f(dy, dx) - s[4];
    angle = fmodf(angle, 2.0f * (float)M_PI);
    if (angle < 0.0f)
        angle += 2.0f * (float)M_PI;
    if (angle > s[5])
        return 0.0f;
    float d = fabsf(sqrtf(dx * dx + dy * dy) - s[2]);
    return clampf(s[3] - d + 0.5f, 0.0f, 1.0f);
}

void gfxArc(float cx, float cy, float radius, float thickness, float start, float end, Color color)
{
    float r = radius + thickness / 2.0f;
    float s[6] = {cx, cy, radius, thickness / 2.0f, start, clampf(end - start, 0.0f, 2.0f * (float)M_PI)};
    fillShape(cx - r, cy - r, cx + r, cy + r, s, arcCoverage, color);
}

// shape: the three corners. 4x4 samples per pixel.
static float triangleCoverage(float px, float py, const float *s)
{
    int inside = 0;
    for (int i = 0; i < 16; i++) {
        float x = px - 0.5f + ((i & 3) + 0.5f) / 4.0f, y = py - 0.5f + ((i >> 2) + 0.5f) / 4.0f;
        float e0 = (s[2] - s[0]) * (y - s[1]) - (s[3] - s[1]) * (x - s[0]);
        float e1 = (s[4] - s[2]) * (y - s[3]) - (s[5] - s[3]) * (x - s[2]);
        float e2 = (s[0] - s[4]) * (y - s[5]) - (s[1] - s[5]) * (x - s[4]);
        inside += (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
    }
    return (float)inside / 16.0f;
}

void gfxTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color)
{
    float s[6] = {x0, y0, x1, y1, x2, y2};
    fillShape(fminf(x0, fminf(x1, x2)), fminf(y0, fminf(y1, y2)), fmaxf(x0, fmaxf(x1, x2)),
              fmaxf(y0, fmaxf(y1, y2)), s, triangleCoverage, color);
}

void gfxBlit(int x, int y, int size, const uint32_t *pixels)
{
    int cx = x, cy = y, w = size, h = size;
    if (!pixels || !clip(&cx, &cy, &w, &h))
        return;
    for (int row = 0; row < h; row++) {
        const uint32_t *src = pixels + (size_t)(cy - y + row) * size + (cx - x);
        memcpy(g_pixels + (size_t)(cy + row) * g_stride + cx, src, (size_t)w * sizeof(uint32_t));
    }
}

void gfxBlendMask(int x, int y, int w, int h, const uint8_t *mask, int stride, Color color)
{
    int cx = x, cy = y, cw = w, ch = h;
    if (!mask || !clip(&cx, &cy, &cw, &ch))
        return;
    for (int row = 0; row < ch; row++) {
        const uint8_t *m = mask + (size_t)(cy - y + row) * stride + (cx - x);
        uint32_t *dst = g_pixels + (size_t)(cy + row) * g_stride + cx;
        for (int i = 0; i < cw; i++)
            blendPixel(&dst[i], color, m[i]);
    }
}
