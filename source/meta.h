// Title, artist and album of the songs in the lists, read in the background.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "metacache.h"

bool metaInit(void);
void metaExit(void);

// Copies the tags of a song (any may be empty). False while they are still
// being read; the song is queued. Cheap to call every frame.
bool metaLookup(const char *path, SongMeta *out);
