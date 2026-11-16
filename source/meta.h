// Artist and album of the songs in the lists, read in the background.
#pragma once

#include <stdbool.h>
#include <stddef.h>

bool metaInit(void);
void metaExit(void);

// Copies the artist and album of a song (either may be empty). False while
// they are still being read; the song is queued. Cheap to call every frame.
bool metaLookup(const char *path, char *artist, size_t artist_size, char *album, size_t album_size);
