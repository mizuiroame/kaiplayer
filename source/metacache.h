// A small cache of the title, artist and album of the songs shown in the lists.
// The UI looks songs up every frame; a background thread fills in the ones
// that are missing, most recently shown first. Not thread-safe by itself:
// meta.c guards it with a lock.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tags.h"
#include "util.h"

#define METACACHE_SIZE 128

typedef enum {
    META_EMPTY,
    META_WANTED,   // Shown, waiting for the thread.
    META_LOADING,  // The thread is reading its tags.
    META_READY,
} MetaState;

// What the lists show from a song's tags; any of it may be empty.
typedef struct {
    char title[TAGS_TEXT_MAX];
    char artist[TAGS_TEXT_MAX];
    char album[TAGS_TEXT_MAX];
} SongMeta;

typedef struct {
    char path[UTIL_PATH_MAX];
    SongMeta meta;
    MetaState state;
    uint64_t last_used;
} MetaEntry;

typedef struct {
    MetaEntry entries[METACACHE_SIZE];
    uint64_t clock;
} MetaCache;

void metaCacheInit(MetaCache *c);
// The entry for path, if its tags are loaded. Otherwise NULL, and the path is
// queued for loading (replacing the least recently shown entry).
const MetaEntry *metaCacheGet(MetaCache *c, const char *path);
// The most recently shown song still waiting; marks it as loading. False if
// there is none.
bool metaCacheTake(MetaCache *c, char *path, size_t size);
// Stores the tags read for path (if it is still in the cache).
void metaCacheStore(MetaCache *c, const char *path, const SongMeta *meta);
