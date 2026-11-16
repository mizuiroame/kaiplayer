// An image of the user's behind every view instead of the plain dark color:
// JPEG or PNG files in the "Backgrounds" folder of the music folder. The
// image is cropped to fill the screen, blurred and darkened once, when it is
// chosen, so text stays readable over it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BACKGROUND_FOLDER_NAME "Backgrounds"
#define BACKGROUND_NAME_MAX 128
#define BACKGROUND_MAX_IMAGES 64

// The image files in <library>/Backgrounds, sorted by name. Returns how many.
size_t backgroundList(const char *library, char names[][BACKGROUND_NAME_MAX], size_t max);

// Loads <library>/Backgrounds/<name> as the background. An empty name, or a
// file that can't be read, leaves none (false for the latter).
bool backgroundLoad(const char *library, const char *name);

// The background, GFX_FB_WIDTH x GFX_FB_HEIGHT pixels, or NULL for none.
const uint32_t *backgroundPixels(void);

// Turns an rgba image (w x h) into a background: crops and scales it to fill
// out_w x out_h, blurs it and mixes it with base (0xAABBGGRR) by dim/255.
// Exposed for tests. False when out of memory.
bool backgroundMake(const uint8_t *rgba, int w, int h, uint32_t *out, int out_w, int out_h, uint32_t base,
                    unsigned dim);
