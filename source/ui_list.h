// Scrolling lists shared by the Browse, Playlists and Queue tabs.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "font.h"
#include "gfx.h"
#include "metacache.h"
#include "ui_hit.h"

#define SUBTITLE_Y 76
#define LIST_Y 108
#define ROW_H 44
#define TEXT_ROW 22
#define TEXT_SMALL 18

// First row to show so that the cursor stays near the middle.
size_t uiFirstVisible(size_t cursor, size_t count);

// A list as a finger left it, as on a phone: where it is (free: not centered
// on the cursor), the list it's for, and what that list was when drawn.
typedef struct {
    bool free;
    const void *key;      // The list (its cursor's address): another one starts centered.
    size_t first;
    int offset;           // Pixels into the first row in view.
    size_t cursor, count; // As the list was drawn last.
    size_t want_cursor;   // Where the cursor should be to stay in view, after a move.
} UiListScroll;
extern UiListScroll g_list_scroll;

// A list starts drawing: the first row in view and pixels into it (offset);
// it's drawn cut to its place, and its targets too, until uiListEnd.
size_t uiListBegin(const void *key, size_t cursor, size_t count, int *offset);
void uiListEnd(void);
// A finger moves the list last drawn by px (positive: up). False at an end.
// want_cursor then says where the cursor goes to stay in view.
bool uiListScrollPx(int px);
// A button moved the cursor: the list keeps it in its middle again.
void uiListRelease(void);
// False while the buttons are on the player bar: lists then draw no
// selection, so only one thing looks selected. Set each frame by uiDraw.
extern bool g_ui_list_focused;
// The highlight behind the selected row, with a bar in the given color.
void uiDrawSelection(int y, Color bar);
void uiDrawScrollbar(size_t first, size_t count);
void uiDrawFolderIcon(int x, int y, Color color);
// Song rows have Artist and Album columns, read from the tags.
#define COLUMN_ARTIST_X 610
#define COLUMN_ALBUM_X 880

typedef struct {
    size_t number;     // Shown on the left (unless current).
    const char *path;
    int y;
    size_t index;      // Row index, for the "+" touch target.
    bool selected;
    bool current;      // The song that is playing (or paused).
    bool playing;      // Animate the bars (false while paused).
    bool add_focused;
    bool remove_button;  // Inside a playlist: the row button removes the song.
    uint64_t now_ns;
} TrackRow;

// A song row: number (or moving bars when current), title, artist, album
// and a "+" button that adds the song to a playlist.
void uiDrawTrackRow(const TrackRow *row);
// What a song row shows: the title from the tags (the file name until they
// are read, or if there is none), artist and album.
typedef struct {
    char title[UTIL_PATH_MAX];
    char folder[UTIL_PATH_MAX];
    SongMeta meta;
    bool loaded;
} UiSong;

void uiLoadSong(const char *path, UiSong *song);
// The song's title, or, while its tags are still being read, a soft bar in
// its place (so the file name doesn't flash before the title).
void uiDrawSongTitle(const UiSong *song, FontFace face, int x, int y, int max_width, Color color);
// The artist and album in their columns (empty until loaded).
void uiDrawSongColumns(const UiSong *song, int y);
// "Artist" and "Album" above the columns, on the subtitle line.
void uiDrawColumnHeaders(void);
// Three bars moving with the music (still when paused), 20x20 at x, y.
void uiDrawPlayingBars(int x, int y, bool playing, uint64_t now_ns);

// "+" at the end of a row, for adding it to a playlist.
#define ADD_BUTTON_W 44
void uiDrawAddButton(int x, int y, size_t index, bool focused);
// The same place, for removing a song from the open playlist.
void uiDrawRemoveButton(int x, int y, size_t index, bool focused);
// "< Back" at the start of the subtitle line. Returns its width.
int uiDrawBackButton(int x, int y, bool focused);
// The same, with the touch target it adds (HIT_OVERLAY_BACK in an overlay).
int uiDrawBackButtonHit(int x, int y, bool focused, HitKind hit);
// Touch targets: the list area (for drags) and one row.
void uiHitListArea(void);
void uiHitRow(int y, size_t index);

// A grey line of text under the header, and a hint in the empty list.
void uiDrawEmptyList(const char *subtitle, const char *hint);
