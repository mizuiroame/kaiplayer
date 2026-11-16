// The list metadata cache: queueing, loading order and eviction.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "metacache.h"

void testMeta(void)
{
    MetaCache *c = malloc(sizeof(MetaCache));
    metaCacheInit(c);
    char path[UTIL_PATH_MAX];

    // Unknown songs are queued, not ready.
    CHECK(metaCacheGet(c, "sdmc:/a.mp3") == NULL);
    CHECK(metaCacheGet(c, "sdmc:/b.mp3") == NULL);
    CHECK(metaCacheGet(c, "sdmc:/a.mp3") == NULL);  // Shown again: a is the most recent.

    // The most recently shown song loads first.
    CHECK(metaCacheTake(c, path, sizeof(path)) && strcmp(path, "sdmc:/a.mp3") == 0);
    CHECK(metaCacheTake(c, path, sizeof(path)) && strcmp(path, "sdmc:/b.mp3") == 0);
    CHECK(!metaCacheTake(c, path, sizeof(path)));

    SongMeta meta = {"Title", "Artist", "Album"};
    metaCacheStore(c, "sdmc:/a.mp3", &meta);
    const MetaEntry *e = metaCacheGet(c, "sdmc:/a.mp3");
    CHECK(e && strcmp(e->meta.title, "Title") == 0 && strcmp(e->meta.artist, "Artist") == 0 &&
          strcmp(e->meta.album, "Album") == 0);
    CHECK(metaCacheGet(c, "sdmc:/b.mp3") == NULL);  // Still loading.
    metaCacheStore(c, "sdmc:/gone.mp3", &meta);   // Not in the cache: ignored.

    // A full cache drops the least recently shown entry, never one loading.
    for (int i = 0; i < METACACHE_SIZE; i++) {
        snprintf(path, sizeof(path), "sdmc:/n%d.mp3", i);
        metaCacheGet(c, path);
    }
    CHECK(metaCacheGet(c, "sdmc:/a.mp3") == NULL);  // Evicted and queued again.
    SongMeta b = {"", "B", ""};
    metaCacheStore(c, "sdmc:/b.mp3", &b);  // b was loading: kept.
    e = metaCacheGet(c, "sdmc:/b.mp3");
    CHECK(e && strcmp(e->meta.artist, "B") == 0 && e->meta.title[0] == '\0');
    free(c);
}
