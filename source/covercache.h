// Album covers for the Home cards and the album rows, loaded on a background
// thread and kept for the most recently shown albums.
#pragma once

#include <stdbool.h>

#include "cover.h"

#define COVERCACHE_CARD 120   // Layout size of a Home card.
#define COVERCACHE_THUMB 30   // Layout size of an album row's icon.

bool coverCacheInit(void);
void coverCacheExit(void);

// Hold the lock while drawing with the covers it returns.
void coverCacheLock(void);
void coverCacheUnlock(void);
// The cover of the album that song_path belongs to, at card or thumb size;
// NULL while it loads (it is queued) or if the album has none.
const CoverImage *coverCacheGet(const char *song_path, bool thumb);
