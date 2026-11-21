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
// The accent changes with the theme (and the cover): set at each frame.
extern Color g_ui_accent;
#define COLOR_ACCENT g_ui_accent
#define COLOR_FOLDER GFX_RGB(0x7C, 0xC4, 0xFF)
#define COLOR_NETWORK GFX_RGB(0x2E, 0xC4, 0xB6)  // The network folder's, as in KaiVideo.
#define COLOR_SERVER GFX_RGB(0x3D, 0x6B, 0xFF)   // The Navidrome server's: a deep blue, not an accent's.
#define COLOR_TEXT GFX_RGB(0xF2, 0xF2, 0xF5)
#define COLOR_DIM GFX_RGB(0xA4, 0xA4, 0xB0)
#define COLOR_FAINT GFX_RGB(0x5E, 0x5E, 0x6A)
#define COLOR_ERROR GFX_RGB(0xFF, 0x8A, 0x5C)
#define COLOR_SHADOW GFX_RGB(0x00, 0x00, 0x00)

#define MARGIN 32
#define HEADER_H 64
#define CONTENT_BOTTOM 664
#define FOOTER_Y 664

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
// A head and shoulders, for artists, centered on (cx, cy).
void uiDrawPersonIcon(float cx, float cy, float size, Color color);

const char *uiRepeatLabel(RepeatMode repeat);

// A rounded label, lit in the accent color when on. 30 pixels high.
int uiPillWidth(const char *label);
void uiDrawPill(int x, int y, const char *label, bool on);

typedef enum {
    PILL_ICON_SHUFFLE,     // Crossed arrows.
    PILL_ICON_REPEAT,      // Arrows in a circle.
    PILL_ICON_REPEAT_ONE,  // The same, with a 1 inside.
    PILL_ICON_PLAYLIST_ADD,
    PILL_ICON_CHECK,
} PillIcon;

// "Add to playlist": a list with a plus, in a box of about 20 * scale.
void uiDrawPlaylistAddIcon(float cx, float cy, float scale, Color color);
// "Remove from playlist": the same list with a minus.
void uiDrawPlaylistRemoveIcon(float cx, float cy, float scale, Color color);

// A pill with an icon before the label, and an optional button hint
// ("X") before the icon.
int uiIconPillWidth(const char *hint, const char *label);
void uiDrawIconPill(int x, int y, const char *hint, PillIcon icon, const char *label, bool on);

// The Playlists tab (ui_playlists.c).
void uiDrawPlaylists(const App *app, const PlayerStatus *status, uint64_t now_ns);
// The Home tab (ui_home.c).
void uiDrawHome(const App *app, const PlayerStatus *status, uint64_t now_ns);
// The Library tab's pages other than Folders (ui_library.c).
void uiDrawLibrary(const App *app, const PlayerStatus *status, uint64_t now_ns);

// The + menu and the keyboard, drawn over everything (ui_overlay.c).
void uiDrawOverlay(const App *app, uint64_t now_ns);
// True while a menu, panel or keyboard is over the view. What's behind it is
// drawn dimmed by UI_DIM_ALPHA, once, and kept still until the overlay closes.
bool uiOverlayOpen(const App *app);
#define UI_DIM_ALPHA 160

// The Now Playing view (ui_now.c).
void uiDrawNowPlaying(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns);
// Over the kept, dimmed view under an overlay: draws the record, CD or
// cassette again, turned to now and dimmed, so it keeps turning.
void uiNowPlayingUnderOverlay(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns);
