// Album art: decodes JPEG or PNG (embedded in a file or next to it) into
// square images, center-cropped and scaled, for drawing.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COVER_MAX_DIMENSION 4096
#define COVER_FILE_MAX (8u << 20)

typedef struct {
    int size;           // Width and height.
    uint32_t *pixels;   // size * size pixels, 0xAABBGGRR like gfx's Color.
    uint32_t average;   // Mean color, for backgrounds.
} CoverImage;

void coverFree(CoverImage *c);

// Decodes an image and scales it to both sizes. On failure both stay empty.
bool coverDecode(const uint8_t *data, size_t size, CoverImage *large, int large_size,
                 CoverImage *small, int small_size);

// Tries cover.jpg, folder.jpg and similar files in the track's folder.
bool coverLoadFromFolder(const char *track_path, CoverImage *large, int large_size,
                         CoverImage *small, int small_size);
