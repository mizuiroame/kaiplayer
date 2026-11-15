#include "gfx.h"

#include <string.h>

#include <switch.h>

#include "font8x8_basic.h"
#include "font8x8_ext_latin.h"

#define FRAMEBUFFER_COUNT 2
#define LATIN1_FIRST 0xA0
#define LATIN1_LAST 0xFF

static Framebuffer g_fb;
static u32 *g_pixels;
static u32 g_stride;  // In pixels.

bool gfxInit(void)
{
    if (R_FAILED(framebufferCreate(&g_fb, nwindowGetDefault(), GFX_WIDTH, GFX_HEIGHT,
                                   PIXEL_FORMAT_RGBA_8888, FRAMEBUFFER_COUNT)))
        return false;
    if (R_FAILED(framebufferMakeLinear(&g_fb))) {
        framebufferClose(&g_fb);
        return false;
    }
    return true;
}

void gfxExit(void)
{
    framebufferClose(&g_fb);
}

void gfxBegin(void)
{
    u32 stride_bytes = 0;
    g_pixels = framebufferBegin(&g_fb, &stride_bytes);
    g_stride = stride_bytes / sizeof(u32);
}

void gfxEnd(void)
{
    if (g_pixels)
        framebufferEnd(&g_fb);
    g_pixels = NULL;
}

void gfxClear(Color color)
{
    gfxFillRect(0, 0, GFX_WIDTH, GFX_HEIGHT, color);
}

void gfxFillRect(int x, int y, int w, int h, Color color)
{
    if (!g_pixels)
        return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > GFX_WIDTH ? GFX_WIDTH : x + w;
    int y1 = y + h > GFX_HEIGHT ? GFX_HEIGHT : y + h;
    for (int py = y0; py < y1; py++) {
        u32 *row = g_pixels + (size_t)py * g_stride;
        for (int px = x0; px < x1; px++)
            row[px] = color;
    }
}

void gfxFillPlayIcon(int x, int y, int w, int h, Color color)
{
    for (int row = 0; row < h; row++) {
        int distance = row < h / 2 ? row : h - 1 - row;
        int span = (int)((long)w * distance * 2 / (h > 1 ? h - 1 : 1));
        gfxFillRect(x, y + row, span > 0 ? span : 1, 1, color);
    }
}

// Reads one UTF-8 code point and advances *s. Bad bytes read as '?'.
static uint32_t nextCodePoint(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp;
    int extra;
    if (p[0] < 0x80) {
        cp = p[0];
        extra = 0;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        extra = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        extra = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        extra = 3;
    } else {
        *s += 1;
        return '?';
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *s += i;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += extra + 1;
    return cp;
}

static const char *glyphFor(uint32_t cp)
{
    if (cp < 128)
        return font8x8_basic[cp];
    if (cp >= LATIN1_FIRST && cp <= LATIN1_LAST)
        return font8x8_ext_latin[cp - LATIN1_FIRST];
    return font8x8_basic['?'];
}

static void drawGlyph(int x, int y, const char *glyph, int scale, Color color)
{
    for (int row = 0; row < GFX_GLYPH; row++) {
        unsigned char bits = (unsigned char)glyph[row];
        for (int col = 0; col < GFX_GLYPH; col++) {
            if (bits & (1u << col))
                gfxFillRect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

int gfxTextWidth(const char *utf8, int scale)
{
    int count = 0;
    while (*utf8) {
        nextCodePoint(&utf8);
        count++;
    }
    return count * GFX_GLYPH * scale;
}

int gfxDrawText(int x, int y, const char *utf8, int scale, Color color)
{
    int start = x;
    while (*utf8) {
        drawGlyph(x, y, glyphFor(nextCodePoint(&utf8)), scale, color);
        x += GFX_GLYPH * scale;
    }
    return x - start;
}

void gfxDrawTextClipped(int x, int y, int max_width, const char *utf8, int scale, Color color)
{
    int advance = GFX_GLYPH * scale;
    int max_chars = max_width / advance;
    if (gfxTextWidth(utf8, scale) <= max_width) {
        gfxDrawText(x, y, utf8, scale, color);
        return;
    }
    int room = max_chars - 3;
    for (int i = 0; i < room && *utf8; i++) {
        drawGlyph(x, y, glyphFor(nextCodePoint(&utf8)), scale, color);
        x += advance;
    }
    gfxDrawText(x, y, "...", scale, color);
}
