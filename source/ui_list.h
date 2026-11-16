// Scrolling lists shared by the Browse, Playlists and Queue tabs.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#define SUBTITLE_Y 76
#define LIST_Y 108
#define ROW_H 44
#define TEXT_ROW 22
#define TEXT_SMALL 18

// First row to show so that the cursor stays near the middle.
size_t uiFirstVisible(size_t cursor, size_t count);
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
    uint64_t now_ns;
} TrackRow;

// A song row: number (or moving bars when current), title, artist, album
// and a "+" button that adds the song to a playlist.
void uiDrawTrackRow(const TrackRow *row);
// The artist and album of a song in their columns (empty until loaded).
void uiDrawSongColumns(const char *path, int y);
// "Artist" and "Album" above the columns, on the subtitle line.
void uiDrawColumnHeaders(void);
// Three bars moving with the music (still when paused), 20x20 at x, y.
void uiDrawPlayingBars(int x, int y, bool playing, uint64_t now_ns);

// "+" at the end of a row, for adding it to a playlist.
#define ADD_BUTTON_W 44
void uiDrawAddButton(int x, int y, size_t index, bool focused);
// "< Back" at the start of the subtitle line. Returns its width.
int uiDrawBackButton(int x, int y, bool focused);
// Touch targets: the list area (for drags) and one row.
void uiHitListArea(void);
void uiHitRow(int y, size_t index);

// A grey line of text under the header, and a hint in the empty list.
void uiDrawEmptyList(const char *subtitle, const char *hint);
