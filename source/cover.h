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
    uint32_t vibrant;   // The most common vivid color; 0 for a black, white or grey cover.
} CoverImage;

void coverFree(CoverImage *c);

// The most common vivid color of some pixels (0xAABBGGRR), or 0 if they
// are black, white or grey. Sets CoverImage.vibrant.
uint32_t coverVibrantColor(const uint32_t *pixels, size_t count);

// Decodes an image and scales it to both sizes. On failure both stay empty.
bool coverDecode(const uint8_t *data, size_t size, CoverImage *large, int large_size,
                 CoverImage *small, int small_size);

// How good a file name is as a folder cover, ignoring case: 0 for
// "cover.*", then "folder", "front", "albumart*", then other images.
// -1 if it is not a JPEG or PNG name. Exposed for tests.
int coverNamePriority(const char *name);

// Finds the cover image in the track's folder (Cover.jpg, folder.png...),
// matching names without regard to case. Falls back to the folder's only
// image if it has an unusual name. A server's song ("sub:/") has its
// album's cover from the server instead, asked for at large_size pixels.
bool coverLoadFromFolder(const char *track_path, CoverImage *large, int large_size,
                         CoverImage *small, int small_size);
