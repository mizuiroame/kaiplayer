// Software drawing into libnx's framebuffer: shapes, gradients and images.
// Text is drawn by font.h on top of these.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GFX_WIDTH 1280
#define GFX_HEIGHT 720

// RGBA8 as libnx stores it: 0xAABBGGRR.
typedef uint32_t Color;
#define GFX_RGB(r, g, b) ((Color)(0xFF000000u | ((b) << 16) | ((g) << 8) | (r)))

// The framebuffer (gfx_switch.c).
bool gfxInit(void);
void gfxExit(void);

// Starts and presents a frame. Drawing is only valid between the two.
void gfxBegin(void);
void gfxEnd(void);

// Sets the pixels that drawing goes to; NULL turns drawing off. stride is
// in pixels. gfxBegin calls this; the host preview uses it directly.
void gfxSetTarget(uint32_t *pixels, uint32_t stride);

// Linear blend from a (t = 0) to b (t = 255).
Color gfxMix(Color a, Color b, unsigned t);

void gfxClear(Color color);
void gfxFillRect(int x, int y, int w, int h, Color color);
// Draws color over what is there with opacity alpha (0-255).
void gfxBlendRect(int x, int y, int w, int h, Color color, unsigned alpha);
void gfxFillRoundRect(int x, int y, int w, int h, int radius, Color color);
void gfxFillCircle(int cx, int cy, int radius, Color color);
void gfxGradientV(int x, int y, int w, int h, Color top, Color bottom);

// Triangles in a w x h box, pointing right or left.
void gfxFillTriangleRight(int x, int y, int w, int h, Color color);
void gfxFillTriangleLeft(int x, int y, int w, int h, Color color);

// Anti-aliased strokes for icons. A line with round ends; an arc of a ring
// from angle start to end (radians, clockwise on screen from 3 o'clock);
// and any triangle.
void gfxLine(float x0, float y0, float x1, float y1, float thickness, Color color);
void gfxArc(float cx, float cy, float radius, float thickness, float start, float end, Color color);
void gfxTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color);

// Copies a size x size image.
void gfxBlit(int x, int y, int size, const uint32_t *pixels);
// Draws color through an 8-bit coverage mask (used for text).
void gfxBlendMask(int x, int y, int w, int h, const uint8_t *mask, int stride, Color color);
