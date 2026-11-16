#include "meta.h"

#include <stdio.h>
#include <stdlib.h>

#include <switch.h>

#define THREAD_STACK_SIZE 0x10000
#define THREAD_PRIORITY 0x31  // Below the artwork thread.

static Mutex g_lock;
static CondVar g_wake;
static Thread g_thread;
static bool g_running;
static bool g_started;
static MetaCache *g_cache;

static void readMeta(const char *path, TrackTags *tags)
{
    tagsInit(tags);
    tags->skip_picture = true;  // Only the text is needed.
    tagsRead(path, tags);
}

static void workerMain(void *arg)
{
    (void)arg;
    char path[UTIL_PATH_MAX];
    TrackTags *tags = malloc(sizeof(TrackTags));
    if (!tags)
        return;
    mutexLock(&g_lock);
    while (g_running) {
        if (!metaCacheTake(g_cache, path, sizeof(path))) {
            condvarWait(&g_wake, &g_lock);
            continue;
        }
        mutexUnlock(&g_lock);
        readMeta(path, tags);
        mutexLock(&g_lock);
        static SongMeta meta;  // Only this thread uses it.
        snprintf(meta.title, sizeof(meta.title), "%s", tags->title);
        snprintf(meta.artist, sizeof(meta.artist), "%s", tags->artist[0] ? tags->artist : tags->album_artist);
        snprintf(meta.album, sizeof(meta.album), "%s", tags->album);
        metaCacheStore(g_cache, path, &meta);
        tagsFree(tags);
    }
    mutexUnlock(&g_lock);
    free(tags);
}

bool metaInit(void)
{
    g_cache = malloc(sizeof(MetaCache));
    if (!g_cache)
        return false;
    metaCacheInit(g_cache);
    mutexInit(&g_lock);
    condvarInit(&g_wake);
    g_running = true;
    if (R_FAILED(threadCreate(&g_thread, workerMain, NULL, NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, -2))) {
        g_running = false;
        return false;
    }
    if (R_FAILED(threadStart(&g_thread))) {
        threadClose(&g_thread);
        g_running = false;
        return false;
    }
    g_started = true;
    return true;
}

void metaExit(void)
{
    if (g_started) {
        mutexLock(&g_lock);
        g_running = false;
        condvarWakeAll(&g_wake);
        mutexUnlock(&g_lock);
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_started = false;
    }
    free(g_cache);
    g_cache = NULL;
}

bool metaLookup(const char *path, SongMeta *out)
{
    if (!g_started)
        return false;
    mutexLock(&g_lock);
    const MetaEntry *e = metaCacheGet(g_cache, path);
    if (e)
        *out = e->meta;
    else
        condvarWakeOne(&g_wake);
    mutexUnlock(&g_lock);
    return e != NULL;
}
