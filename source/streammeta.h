// The title, artist and album of the online songs shown (stream.h), from the services' answers, for the
// lists and Now Playing: their files aren't read for them. Kept for the session, the newest
// STREAM_META_MAX; after that (or a restart) the files' own tags are read instead. Thread-safe.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define STREAM_META_MAX 2048

// Keeps (or replaces) a song's names. False if memory ran out.
bool streamMetaPut(const char *location, const char *title, const char *artist, const char *album);
// A song's names, if they're kept.
bool streamMetaGet(const char *location, char *title, size_t title_size, char *artist, size_t artist_size,
                   char *album, size_t album_size);
// Forgets them all (KaiPlayer ends; the tests).
void streamMetaClear(void);
