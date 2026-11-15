// Colors, layout and helpers shared by the views.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app.h"
#include "artwork.h"
#include "font.h"
#include "gfx.h"
#include "player.h"
#include "util.h"

#define COLOR_BG GFX_RGB(0x12, 0x12, 0x14)
#define COLOR_SURFACE GFX_RGB(0x1C, 0x1C, 0x21)
#define COLOR_SURFACE_HI GFX_RGB(0x2C, 0x2C, 0x34)
#define COLOR_ACCENT GFX_RGB(0xFF, 0x6B, 0x9A)
#define COLOR_FOLDER GFX_RGB(0x7C, 0xC4, 0xFF)
#define COLOR_TEXT GFX_RGB(0xF2, 0xF2, 0xF5)
#define COLOR_DIM GFX_RGB(0xA4, 0xA4, 0xB0)
#define COLOR_FAINT GFX_RGB(0x5E, 0x5E, 0x6A)
#define COLOR_ERROR GFX_RGB(0xFF, 0x8A, 0x5C)
#define COLOR_SHADOW GFX_RGB(0x00, 0x00, 0x00)

#define MARGIN 32
#define HEADER_H 64
#define CONTENT_BOTTOM 672
#define FOOTER_Y 672

// What the UI shows about the current track: tags when the artwork loader
// has them, otherwise names derived from the path.
typedef struct {
    bool has_track;
    char title[UTIL_PATH_MAX];
    char artist[UTIL_PATH_MAX];
    char album[UTIL_PATH_MAX];
    char folder[UTIL_PATH_MAX];
    const CoverImage *cover;  // NULL when there is none. Valid while artwork is locked.
    const CoverImage *thumb;
} UiTrack;

// Splits a path into a title (file name without extension) and the name of
// its folder. All three buffers have UTIL_PATH_MAX bytes.
void uiSplitTrackPath(const char *path, char *title, char *folder);

// Fills track from the player status and locked artwork (which may be NULL).
void uiResolveTrack(const PlayerStatus *status, const Artwork *art, UiTrack *track);

// Draws a cover, or a placeholder with a music note.
void uiDrawCover(int x, int y, int size, const CoverImage *cover);
void uiDrawNoteIcon(int x, int y, int size, Color color);

const char *uiRepeatLabel(RepeatMode repeat);

// The Now Playing view (ui_now.c).
void uiDrawNowPlaying(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns);
