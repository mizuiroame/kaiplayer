#include "covercache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "gfx.h"
#include "tags.h"
#include "util.h"

#define CACHE_SIZE 32
#define THREAD_STACK_SIZE 0x40000  // Roomy: a network folder's reads (libsmb2, curl) run on it.
#define THREAD_PRIORITY 0x31

typedef enum {
    SLOT_EMPTY,
    SLOT_WANTED,
    SLOT_LOADING,
    SLOT_READY,  // card and thumb are set, or has_cover is false.
} SlotState;

typedef struct {
    char path[UTIL_PATH_MAX];
    SlotState state;
    bool has_cover;
    CoverImage card, thumb;
    uint64_t last_used;
} Slot;

static Mutex g_lock;
static CondVar g_wake;
static Thread g_thread;
static bool g_started, g_running;
static Slot *g_slots;
static uint64_t g_clock;

static Slot *find(const char *path)
{
    for (int i = 0; i < CACHE_SIZE; i++)
        if (g_slots[i].state != SLOT_EMPTY && strcmp(g_slots[i].path, path) == 0)
            return &g_slots[i];
    return NULL;
}

// An empty slot, or the least recently shown one not being loaded.
static Slot *victim(void)
{
    Slot *best = NULL;
    for (int i = 0; i < CACHE_SIZE; i++) {
        Slot *s = &g_slots[i];
        if (s->state == SLOT_EMPTY)
            return s;
        if (s->state != SLOT_LOADING && (!best || s->last_used < best->last_used))
            best = s;
    }
    return best;
}

static void clearSlot(Slot *s)
{
    coverFree(&s->card);
    coverFree(&s->thumb);
    s->has_cover = false;
}

static void load(const char *path, CoverImage *card, CoverImage *thumb, bool *has_cover)
{
    TrackTags tags;
    tagsInit(&tags);
    tagsRead(path, &tags);
    int card_size = gfxToFb(COVERCACHE_CARD), thumb_size = gfxToFb(COVERCACHE_THUMB);  // Drawn 1:1.
    *has_cover = coverDecode(tags.picture, tags.picture_size, card, card_size, thumb, thumb_size) ||
                 coverLoadFromFolder(path, card, card_size, thumb, thumb_size);
    tagsFree(&tags);
}

static void workerMain(void *arg)
{
    (void)arg;
    char path[UTIL_PATH_MAX];
    mutexLock(&g_lock);
    while (g_running) {
        Slot *next = NULL;
        for (int i = 0; i < CACHE_SIZE; i++) {  // The most recently shown first.
            Slot *s = &g_slots[i];
            if (s->state == SLOT_WANTED && (!next || s->last_used > next->last_used))
                next = s;
        }
        if (!next) {
            condvarWait(&g_wake, &g_lock);
            continue;
        }
        next->state = SLOT_LOADING;
        snprintf(path, sizeof(path), "%s", next->path);
        mutexUnlock(&g_lock);

        CoverImage card, thumb;
        bool has_cover = false;
        load(path, &card, &thumb, &has_cover);

        mutexLock(&g_lock);
        // Loading slots are never reused, so next is still this path's slot.
        clearSlot(next);
        next->card = card;
        next->thumb = thumb;
        next->has_cover = has_cover;
        next->state = SLOT_READY;
    }
    mutexUnlock(&g_lock);
}

bool coverCacheInit(void)
{
    g_slots = calloc(CACHE_SIZE, sizeof(Slot));
    if (!g_slots)
        return false;
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

void coverCacheExit(void)
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
    for (int i = 0; g_slots && i < CACHE_SIZE; i++)
        clearSlot(&g_slots[i]);
    free(g_slots);
    g_slots = NULL;
}

void coverCacheLock(void)
{
    if (g_started)
        mutexLock(&g_lock);
}

void coverCacheUnlock(void)
{
    if (g_started)
        mutexUnlock(&g_lock);
}

const CoverImage *coverCacheGet(const char *song_path, bool thumb)
{
    if (!g_started)
        return NULL;
    g_clock++;
    Slot *s = find(song_path);
    if (!s) {
        s = victim();
        if (!s || strlen(song_path) >= sizeof(s->path))
            return NULL;
        clearSlot(s);
        snprintf(s->path, sizeof(s->path), "%s", song_path);
        s->state = SLOT_WANTED;
        condvarWakeOne(&g_wake);
    }
    s->last_used = g_clock;
    if (s->state != SLOT_READY || !s->has_cover)
        return NULL;
    return thumb ? &s->thumb : &s->card;
}
