#include "metacache.h"

#include <stdio.h>
#include <string.h>

void metaCacheInit(MetaCache *c)
{
    memset(c, 0, sizeof(*c));
}

static MetaEntry *find(MetaCache *c, const char *path)
{
    for (int i = 0; i < METACACHE_SIZE; i++) {
        if (c->entries[i].state != META_EMPTY && strcmp(c->entries[i].path, path) == 0)
            return &c->entries[i];
    }
    return NULL;
}

// An empty slot, or the least recently shown one that isn't being loaded.
static MetaEntry *victim(MetaCache *c)
{
    MetaEntry *best = NULL;
    for (int i = 0; i < METACACHE_SIZE; i++) {
        MetaEntry *e = &c->entries[i];
        if (e->state == META_EMPTY)
            return e;
        if (e->state != META_LOADING && (!best || e->last_used < best->last_used))
            best = e;
    }
    return best;
}

const MetaEntry *metaCacheGet(MetaCache *c, const char *path)
{
    c->clock++;
    MetaEntry *e = find(c, path);
    if (!e) {
        e = victim(c);
        if (!e || snprintf(e->path, sizeof(e->path), "%s", path) >= (int)sizeof(e->path))
            return NULL;
        memset(&e->meta, 0, sizeof(e->meta));
        e->state = META_WANTED;
    }
    e->last_used = c->clock;
    return e->state == META_READY ? e : NULL;
}

bool metaCacheTake(MetaCache *c, char *path, size_t size)
{
    MetaEntry *best = NULL;
    for (int i = 0; i < METACACHE_SIZE; i++) {
        MetaEntry *e = &c->entries[i];
        if (e->state == META_WANTED && (!best || e->last_used > best->last_used))
            best = e;
    }
    if (!best)
        return false;
    best->state = META_LOADING;
    snprintf(path, size, "%s", best->path);
    return true;
}

void metaCacheStore(MetaCache *c, const char *path, const SongMeta *meta)
{
    MetaEntry *e = find(c, path);
    if (!e)
        return;
    e->meta = *meta;
    e->state = META_READY;
}
