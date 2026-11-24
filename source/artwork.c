#include "artwork.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "gfx.h"
#include "kfile.h"
#include "subsonic.h"

#define THREAD_STACK_SIZE 0x40000  // Roomy: a network folder's reads (libsmb2, curl) run on it.
#define THREAD_PRIORITY 0x30  // Below the UI and audio threads.

static Mutex g_lock;
static CondVar g_wake;
static Thread g_thread;
static bool g_running;
static bool g_started;
static char g_requested[UTIL_PATH_MAX];
static bool g_pending;
static Artwork *g_current;

static void freeArtwork(Artwork *a)
{
    if (!a)
        return;
    coverFree(&a->cover);
    coverFree(&a->thumb);
    lyricsFree(&a->lyrics);
    free(a);
}

// The song's .lrc, on the card or in the network folder (a server's songs have none).
static void loadLyrics(Artwork *a, const char *path)
{
    char lrc[UTIL_PATH_MAX];
    if (subIsLocation(path) || !lyricsPathFor(path, lrc, sizeof(lrc)))
        return;
    size_t size = 0;
    uint8_t *text = kfileReadAll(lrc, LYRICS_FILE_MAX, &size, NULL);
    if (text)
        a->has_lyrics = lyricsParse((const char *)text, size, &a->lyrics);
    free(text);
}

static Artwork *loadArtwork(const char *path)
{
    Artwork *a = calloc(1, sizeof(Artwork));
    if (!a)
        return NULL;
    snprintf(a->path, sizeof(a->path), "%s", path);

    TrackTags tags;
    tagsInit(&tags);
    tags.skip_picture = subIsLocation(path);  // A server's song: its cover comes from the server.
    tagsRead(path, &tags);
    snprintf(a->title, sizeof(a->title), "%s", tags.title);
    snprintf(a->artist, sizeof(a->artist), "%s", tags.artist[0] ? tags.artist : tags.album_artist);
    snprintf(a->album, sizeof(a->album), "%s", tags.album);

    int large = gfxToFb(ARTWORK_LARGE_SIZE), small = gfxToFb(ARTWORK_SMALL_SIZE);  // Drawn 1:1.
    a->has_cover = coverDecode(tags.picture, tags.picture_size, &a->cover, large, &a->thumb, small) ||
                   coverLoadFromFolder(path, &a->cover, large, &a->thumb, small);
    tagsFree(&tags);
    loadLyrics(a, path);
    return a;
}

static void workerMain(void *arg)
{
    (void)arg;
    char path[UTIL_PATH_MAX];
    mutexLock(&g_lock);
    while (g_running) {
        if (!g_pending) {
            condvarWait(&g_wake, &g_lock);
            continue;
        }
        snprintf(path, sizeof(path), "%s", g_requested);
        g_pending = false;
        mutexUnlock(&g_lock);

        Artwork *loaded = loadArtwork(path);

        mutexLock(&g_lock);
        if (loaded && !g_pending) {
            freeArtwork(g_current);
            g_current = loaded;
        } else {
            freeArtwork(loaded);  // A newer request replaced this one.
            if (!loaded && !g_pending)
                g_requested[0] = '\0';  // Out of memory: let the next request retry.
        }
    }
    mutexUnlock(&g_lock);
}

bool artworkInit(void)
{
    mutexInit(&g_lock);
    condvarInit(&g_wake);
    g_running = true;
    if (R_FAILED(threadCreate(&g_thread, workerMain, NULL, NULL, THREAD_STACK_SIZE,
                              THREAD_PRIORITY, -2))) {
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

void artworkExit(void)
{
    if (!g_started)
        return;
    mutexLock(&g_lock);
    g_running = false;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    freeArtwork(g_current);
    g_current = NULL;
}

void artworkRequest(const char *path)
{
    mutexLock(&g_lock);
    bool same = strcmp(path, g_requested) == 0;
    if (!same && g_running) {
        snprintf(g_requested, sizeof(g_requested), "%s", path);
        g_pending = true;
        condvarWakeOne(&g_wake);
    }
    mutexUnlock(&g_lock);
}

const Artwork *artworkLock(void)
{
    mutexLock(&g_lock);
    return g_current;
}

void artworkUnlock(void)
{
    mutexUnlock(&g_lock);
}
