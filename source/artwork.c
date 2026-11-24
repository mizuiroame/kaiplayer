#include "artwork.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "gfx.h"
#include "kfile.h"
#include "subnet.h"
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
static volatile bool g_newer;  // A newer request came: a server's lyrics being asked for stop.

static void freeArtwork(Artwork *a)
{
    if (!a)
        return;
    coverFree(&a->cover);
    coverFree(&a->thumb);
    lyricsFree(&a->lyrics);
    free(a);
}

// The song's lyrics: a .lrc next to it (on the card or in the network folder)
// comes first, then the song's own (tags: USLT, LYRICS), which may be synced
// LRC text too. A server's song asks the server later (serverLyrics), so its
// cover doesn't wait for that.
static void loadLyrics(Artwork *a, const char *path, const TrackTags *tags)
{
    if (subIsLocation(path))
        return;
    char lrc[UTIL_PATH_MAX];
    if (lyricsPathFor(path, lrc, sizeof(lrc))) {
        size_t size = 0;
        uint8_t *text = kfileReadAll(lrc, LYRICS_FILE_MAX, &size, NULL);
        if (text)
            a->has_lyrics = lyricsParse((const char *)text, size, &a->lyrics);
        free(text);
    }
    if (!a->has_lyrics && tags->lyrics)
        a->has_lyrics = lyricsParse(tags->lyrics, tags->lyrics_size, &a->lyrics);
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
    tags.want_lyrics = !tags.skip_picture;
    tagsRead(path, &tags);
    snprintf(a->title, sizeof(a->title), "%s", tags.title);
    snprintf(a->artist, sizeof(a->artist), "%s", tags.artist[0] ? tags.artist : tags.album_artist);
    snprintf(a->album, sizeof(a->album), "%s", tags.album);

    int large = gfxToFb(ARTWORK_LARGE_SIZE), small = gfxToFb(ARTWORK_SMALL_SIZE);  // Drawn 1:1.
    a->has_cover = coverDecode(tags.picture, tags.picture_size, &a->cover, large, &a->thumb, small) ||
                   coverLoadFromFolder(path, &a->cover, large, &a->thumb, small);
    loadLyrics(a, path, &tags);
    tagsFree(&tags);
    return a;
}

// A server's song's lyrics, asked for after its cover and tags are shown,
// and put with them if it's still the song shown. Called and returns with
// g_lock held; a newer request stops the asking.
static void serverLyrics(const char *path)
{
    g_newer = false;
    mutexUnlock(&g_lock);
    char *text = subFetchLyrics(path, &g_newer);
    Lyrics lyrics;
    bool have = text && lyricsParse(text, strlen(text), &lyrics);
    free(text);
    mutexLock(&g_lock);
    if (!have)
        return;
    if (g_pending || !g_current || strcmp(g_current->path, path) != 0) {
        lyricsFree(&lyrics);
        return;
    }
    lyricsFree(&g_current->lyrics);
    g_current->lyrics = lyrics;
    g_current->has_lyrics = true;
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
            if (subIsLocation(path))
                serverLyrics(path);  // Shown first, then its lyrics from the server.
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
        g_newer = true;
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
