#include "streammeta.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *location, *title, *artist, *album;
} Entry;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Entry g_entries[STREAM_META_MAX];
static size_t g_next;  // The slot the next new song takes (the oldest goes).

static void clearEntry(Entry *e)
{
    free(e->location);
    free(e->title);
    free(e->artist);
    free(e->album);
    memset(e, 0, sizeof(*e));
}

static char *copy(const char *s)
{
    return strdup(s ? s : "");
}

static Entry *find(const char *location)
{
    for (size_t i = 0; i < STREAM_META_MAX; i++)
        if (g_entries[i].location && !strcmp(g_entries[i].location, location))
            return &g_entries[i];
    return NULL;
}

bool streamMetaPut(const char *location, const char *title, const char *artist, const char *album)
{
    if (!location || !location[0])
        return false;
    Entry fresh = {copy(location), copy(title), copy(artist), copy(album)};
    if (!fresh.location || !fresh.title || !fresh.artist || !fresh.album) {
        clearEntry(&fresh);
        return false;
    }
    pthread_mutex_lock(&g_lock);
    Entry *e = find(location);
    if (!e) {
        e = &g_entries[g_next];
        g_next = (g_next + 1) % STREAM_META_MAX;
    }
    clearEntry(e);
    *e = fresh;
    pthread_mutex_unlock(&g_lock);
    return true;
}

bool streamMetaGet(const char *location, char *title, size_t title_size, char *artist, size_t artist_size,
                   char *album, size_t album_size)
{
    if (!location)
        return false;
    pthread_mutex_lock(&g_lock);
    const Entry *e = find(location);
    if (e) {
        snprintf(title, title_size, "%s", e->title);
        snprintf(artist, artist_size, "%s", e->artist);
        snprintf(album, album_size, "%s", e->album);
    }
    pthread_mutex_unlock(&g_lock);
    return e != NULL;
}

void streamMetaClear(void)
{
    pthread_mutex_lock(&g_lock);
    for (size_t i = 0; i < STREAM_META_MAX; i++)
        clearEntry(&g_entries[i]);
    g_next = 0;
    pthread_mutex_unlock(&g_lock);
}
