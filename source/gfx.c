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
