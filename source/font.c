#include "font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "font_bold_ttf.h"
#include "font_regular_ttf.h"
#include "stb_truetype.h"
#include "util.h"

#define CACHE_SLOTS 4096
#define CACHE_MAX_USED (CACHE_SLOTS * 3 / 4)
#define ELLIPSIS "\xE2\x80\xA6"

typedef struct {
    bool used;
    uint8_t face;
    uint8_t px;
    uint32_t cp;
    int glyph;  // Glyph index in the font.
    int w, h, xoff, yoff;
    float advance;
    uint8_t *bitmap;
} Glyph;

typedef struct {
    stbtt_fontinfo info;
    int ascent;  // In font units.
    bool ok;
} Face;

static Face g_faces[FONT_FACE_COUNT];
static Glyph g_cache[CACHE_SLOTS];
static int g_cache_used;

bool fontInit(void)
{
    const unsigned char *data[FONT_FACE_COUNT] = {font_regular_ttf, font_bold_ttf};
    for (int i = 0; i < FONT_FACE_COUNT; i++) {
        Face *f = &g_faces[i];
        f->ok = stbtt_InitFont(&f->info, data[i], stbtt_GetFontOffsetForIndex(data[i], 0)) != 0;
        if (!f->ok)
            return false;
        int descent, gap;
        stbtt_GetFontVMetrics(&f->info, &f->ascent, &descent, &gap);
    }
    return true;
}

static void flushCache(void)
{
    for (int i = 0; i < CACHE_SLOTS; i++)
        free(g_cache[i].bitmap);
    memset(g_cache, 0, sizeof(g_cache));
    g_cache_used = 0;
}

void fontExit(void)
{
    flushCache();
}

static float scaleFor(FontFace face, int px)
{
    return stbtt_ScaleForPixelHeight(&g_faces[face].info, (float)px);
}

static void renderGlyph(Glyph *g, FontFace face, int px, uint32_t cp)
{
    Face *f = &g_faces[face];
    float scale = scaleFor(face, px);
    g->glyph = stbtt_FindGlyphIndex(&f->info, (int)cp);
    if (g->glyph == 0)
        g->glyph = stbtt_FindGlyphIndex(&f->info, '?');
    int advance, lsb;
    stbtt_GetGlyphHMetrics(&f->info, g->glyph, &advance, &lsb);
    g->advance = advance * scale;
    g->bitmap = stbtt_GetGlyphBitmap(&f->info, scale, scale, g->glyph, &g->w, &g->h, &g->xoff, &g->yoff);
}

static Glyph *getGlyph(FontFace face, int px, uint32_t cp)
{
    if (g_cache_used >= CACHE_MAX_USED)
        flushCache();
    uint32_t slot = (cp * 2654435761u ^ (uint32_t)px * 40503u ^ (uint32_t)face) % CACHE_SLOTS;
    for (;;) {
        Glyph *g = &g_cache[slot];
        if (!g->used) {
            *g = (Glyph){.used = true, .face = (uint8_t)face, .px = (uint8_t)px, .cp = cp};
            renderGlyph(g, face, px, cp);
            g_cache_used++;
            return g;
        }
        if (g->cp == cp && g->px == px && g->face == face)
            return g;
        slot = (slot + 1) % CACHE_SLOTS;
    }
}

// prev is a glyph index, not a cache entry: the cache may flush mid-string.
static float kerning(FontFace face, int px, int prev, const Glyph *next)
{
    if (prev < 0)
        return 0.0f;
    return stbtt_GetGlyphKernAdvance(&g_faces[face].info, prev, next->glyph) * scaleFor(face, px);
}

int fontTextWidth(FontFace face, int px, const char *utf8)
{
    if (!g_faces[face].ok)
        return 0;
    float x = 0.0f;
    int prev = -1;
    while (*utf8) {
        Glyph *g = getGlyph(face, px, utilNextCodePoint(&utf8));
        x += kerning(face, px, prev, g) + g->advance;
        prev = g->glyph;
    }
    return (int)ceilf(x);
}

// Draws up to max_width pixels of text; returns the width used.
static float drawRun(FontFace face, int px, float x, int y, const char *utf8, float max_width, Color color)
{
    int baseline = y + (int)lroundf(g_faces[face].ascent * scaleFor(face, px));
    float start = x;
    int prev = -1;
    while (*utf8) {
        Glyph *g = getGlyph(face, px, utilNextCodePoint(&utf8));
        float next_x = x + kerning(face, px, prev, g);
        if (next_x + g->advance - start > max_width)
            break;
        gfxBlendMask((int)lroundf(next_x) + g->xoff, baseline + g->yoff, g->w, g->h, g->bitmap, g->w, color);
        x = next_x + g->advance;
        prev = g->glyph;
    }
    return x - start;
}

int fontDraw(FontFace face, int px, int x, int y, const char *utf8, Color color)
{
    if (!g_faces[face].ok)
        return 0;
    return (int)ceilf(drawRun(face, px, (float)x, y, utf8, (float)GFX_WIDTH * 4, color));
}

void fontDrawClipped(FontFace face, int px, int x, int y, int max_width, const char *utf8, Color color)
{
    if (!g_faces[face].ok || max_width <= 0)
        return;
    if (fontTextWidth(face, px, utf8) <= max_width) {
        fontDraw(face, px, x, y, utf8, color);
        return;
    }
    float ellipsis = (float)fontTextWidth(face, px, ELLIPSIS);
    if (ellipsis > (float)max_width)
        return;
    float used = drawRun(face, px, (float)x, y, utf8, (float)max_width - ellipsis, color);
    drawRun(face, px, (float)x + used, y, ELLIPSIS, ellipsis + 1.0f, color);
}
