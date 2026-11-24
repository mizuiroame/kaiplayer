// Loads the tags and cover of the current track on a background thread, so
// that neither the UI nor the audio waits for image decoding.
#pragma once

#include <stdbool.h>

#include "cover.h"
#include "lyrics.h"
#include "tags.h"
#include "util.h"

#define ARTWORK_LARGE_SIZE 400
#define ARTWORK_SMALL_SIZE 72

typedef struct {
    char path[UTIL_PATH_MAX];  // Track these belong to; empty if none.
    char title[TAGS_TEXT_MAX];
    char artist[TAGS_TEXT_MAX];
    char album[TAGS_TEXT_MAX];
    bool has_cover;
    CoverImage cover;
    CoverImage thumb;
    bool has_lyrics;  // A .lrc next to the song, the song's own, or the server's.
    Lyrics lyrics;
} Artwork;

bool artworkInit(void);
void artworkExit(void);

// Asks for the artwork of a track. Cheap to call every frame.
void artworkRequest(const char *path);

// Locks and returns the latest loaded artwork; call artworkUnlock after
// drawing. The data may belong to an older track than the one requested.
const Artwork *artworkLock(void);
void artworkUnlock(void);
