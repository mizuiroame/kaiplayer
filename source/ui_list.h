// Scrolling lists shared by the Browse, Playlists and Queue tabs.
#pragma once

#include <stdbool.h>
#include <stddef.h>

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
// A song row: number (or a play mark when current), title and folder.
void uiDrawTrackRow(size_t number, const char *path, int y, bool selected, bool current);
// Touch targets: the list area (for drags) and one row.
void uiHitListArea(void);
void uiHitRow(int y, size_t index);

// A grey line of text under the header, and a hint in the empty list.
void uiDrawEmptyList(const char *subtitle, const char *hint);
