// The Home tab's rows, worked out from the catalog and the play history.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "catalog.h"
#include "history.h"
#include "playlist.h"

#define HOME_ROW_MAX 4  // Cards per section: the four sections fit on one screen.

typedef enum {
    HOME_TOP_ARTISTS,      // Most played.
    HOME_RECENT_ALBUMS,    // Or, with no history yet, albums from the library.
    HOME_RECENT_ARTISTS,
    HOME_RECENT_PLAYLISTS,
    HOME_ROW_COUNT,
} HomeRowKind;

typedef struct {
    size_t items[HOME_ROW_MAX];  // Artist or album indexes in the catalog.
    size_t count;
} HomeRow;

typedef struct {
    HomeRow rows[HOME_RECENT_PLAYLISTS];
    char playlists[HOME_ROW_MAX][PLAYLIST_NAME_MAX + 1];
    size_t playlist_count;
    bool albums_from_library;  // The albums row is a sample of the library, not history.
} Home;

// c may be NULL (no catalog yet): only playlists are filled.
void homeCompute(Home *home, const Catalog *c, const History *h);
// How many items a row has.
size_t homeRowCount(const Home *home, HomeRowKind row);

// The sections on screen, as a 2x2 grid: top artists and playlists on top,
// albums and artists below. HOME_GRID[i] is the section at place i (0 top
// left, 1 top right, 2 bottom left, 3 bottom right).
extern const HomeRowKind HOME_GRID[HOME_ROW_COUNT];
// The place of a section in that grid.
int homeGridPlace(HomeRowKind row);
