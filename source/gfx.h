// Software drawing into libnx's framebuffer: rectangles and 8x8 bitmap text.
// Text is UTF-8; ASCII and Latin-1 (accents, ñ) are drawn, others show '?'.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GFX_WIDTH 1280
#define GFX_HEIGHT 720
#define GFX_GLYPH 8

// RGBA8 as libnx stores it: 0xAABBGGRR.
typedef uint32_t Color;
#define GFX_RGB(r, g, b) ((Color)(0xFF000000u | ((b) << 16) | ((g) << 8) | (r)))

bool gfxInit(void);
void gfxExit(void);

// Starts and presents a frame. Drawing is only valid between the two.
void gfxBegin(void);
void gfxEnd(void);

void gfxClear(Color color);
void gfxFillRect(int x, int y, int w, int h, Color color);
// A right-pointing triangle inside a w x h box.
void gfxFillPlayIcon(int x, int y, int w, int h, Color color);

int gfxTextWidth(const char *utf8, int scale);
// Draws text; returns its width in pixels.
int gfxDrawText(int x, int y, const char *utf8, int scale, Color color);
// Draws text cut to max_width, ending in "..." when it does not fit.
void gfxDrawTextClipped(int x, int y, int max_width, const char *utf8, int scale, Color color);
