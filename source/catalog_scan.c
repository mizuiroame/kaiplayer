#include "catalog_scan.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "library.h"
#include "tracklist.h"
#include "util.h"

#define THREAD_STACK_SIZE 0x20000
#define THREAD_PRIORITY 0x32  // Below the artwork and list-tag threads.
#define CACHE_NAME "catalog.txt"

static Mutex g_lock;
static CondVar g_wake;
static Thread g_thread;
static bool g_started, g_running;
static char g_root[UTIL_PATH_MAX];  // Requested root.
static bool g_pending;
static Catalog *g_current;          // Published; read under g_lock.
static Mutex g_progress_lock;     // Separate: read while the catalog is locked.
static bool g_scanning;
static size_t g_done, g_total;
static unsigned g_generation;   // Bumped by each publish.

static bool restartRequested(void)
{
    mutexLock(&g_lock);
    bool pending = g_pending || !g_running;
    mutexUnlock(&g_lock);
    return pending;
}

static void publish(Catalog *fresh)
{
    if (fresh)
        fresh->generation = ++g_generation ? g_generation : ++g_generation;  // Never 0.
    mutexLock(&g_lock);
    Catalog *old = g_current;
    g_current = fresh;
    mutexUnlock(&g_lock);
    if (old) {
        catalogFree(old);
        free(old);
    }
}

// catalog.txt in KaiPlayer's folder.
static void cachePath(char *out, size_t size)
{
    snprintf(out, size, "%s/" CACHE_NAME, configFolder());
}

// The saved catalog, keeping only songs inside root. NULL if there is none.
static Catalog *loadCache(const char *root)
{
    char path[UTIL_PATH_MAX];
    cachePath(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    Catalog all, *kept = malloc(sizeof(Catalog));
    catalogInit(&all);
    bool ok = kept && catalogLoad(&all, f);
    fclose(f);
    if (ok) {
        catalogInit(kept);
        for (size_t i = 0; i < all.song_count && ok; i++) {
            if (utilPathWithin(all.songs[i].path, root))
                ok = catalogAddFields(kept, &all.songs[i]);
        }
        ok = ok && kept->song_count > 0 && catalogBuild(kept);
        if (!ok)
            catalogFree(kept);
    }
    catalogFree(&all);
    if (!ok) {
        free(kept);
        return NULL;
    }
    return kept;
}

static void saveCache(const Catalog *c)
{
    char path[UTIL_PATH_MAX], temp[UTIL_PATH_MAX + 8];
    cachePath(path, sizeof(path));
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *f = fopen(temp, "w");
    if (!f)
        return;
    bool ok = catalogSave(c, f);
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);
        rename(temp, path);
    }
}

// Adds one file to fresh: from the previous catalog if it hasn't changed,
// else from its tags.
static bool addFile(Catalog *fresh, const Catalog *previous, const char *path, const char *root, TrackTags *tags)
{
    struct stat st;
    long size = stat(path, &st) == 0 ? (long)st.st_size : -1;
    long known = previous ? catalogFind(previous, path) : -1;
    if (known >= 0 && previous->songs[known].size == size)
        return catalogAddFields(fresh, &previous->songs[known]);
    tagsInit(tags);
    tags->skip_picture = true;
    tagsRead(path, tags);
    bool ok = catalogAdd(fresh, path, size, tags, root);
    tagsFree(tags);
    return ok;
}

// Scans root into a new catalog. NULL if it was cancelled or failed.
static Catalog *scan(const char *root, const Catalog *previous)
{
    TrackList files;
    if (!libraryScan(&files, root, true))
        return NULL;
    mutexLock(&g_progress_lock);
    g_total = files.count;
    mutexUnlock(&g_progress_lock);

    Catalog *fresh = malloc(sizeof(Catalog));
    TrackTags *tags = malloc(sizeof(TrackTags));
    bool ok = fresh && tags;
    if (fresh)
        catalogInit(fresh);
    for (size_t i = 0; i < files.count && ok; i++) {
        if (restartRequested()) {
            ok = false;
            break;
        }
        ok = addFile(fresh, previous, files.paths[i], root, tags);
        mutexLock(&g_progress_lock);
        g_done = i + 1;
        mutexUnlock(&g_progress_lock);
    }
    trackListFree(&files);
    free(tags);
    ok = ok && catalogBuild(fresh);
    if (!ok && fresh) {
        catalogFree(fresh);
        free(fresh);
        return NULL;
    }
    return fresh;
}

static void workerMain(void *arg)
{
    (void)arg;
    char root[UTIL_PATH_MAX];
    static char scanned_root[UTIL_PATH_MAX];  // The folder g_current belongs to.
    mutexLock(&g_lock);
    while (g_running) {
        if (!g_pending) {
            condvarWait(&g_wake, &g_lock);
            continue;
        }
        snprintf(root, sizeof(root), "%s", g_root);
        g_pending = false;
        mutexUnlock(&g_lock);
        mutexLock(&g_progress_lock);
        g_scanning = true;
        g_done = g_total = 0;
        mutexUnlock(&g_progress_lock);

        // Another folder: its catalog starts over. Only this thread replaces
        // or frees g_current, so it can also read it unlocked below.
        if (strcmp(root, scanned_root) != 0) {
            publish(NULL);
            snprintf(scanned_root, sizeof(scanned_root), "%s", root);
        }
        if (!g_current) {
            Catalog *cached = loadCache(root);  // Shown while the scan runs.
            if (cached)
                publish(cached);
        }
        Catalog *fresh = scan(root, g_current);
        if (fresh) {
            saveCache(fresh);
            publish(fresh);
        }

        mutexLock(&g_progress_lock);
        g_scanning = false;
        mutexUnlock(&g_progress_lock);
        mutexLock(&g_lock);
    }
    mutexUnlock(&g_lock);
}

bool catalogScanInit(void)
{
    mutexInit(&g_lock);
    mutexInit(&g_progress_lock);
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

void catalogScanExit(void)
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
    publish(NULL);
}

void catalogScanStart(const char *root)
{
    if (!g_started)
        return;
    mutexLock(&g_lock);
    snprintf(g_root, sizeof(g_root), "%s", root);
    g_pending = true;
    condvarWakeOne(&g_wake);
    mutexUnlock(&g_lock);
}

const Catalog *catalogLock(void)
{
    mutexLock(&g_lock);
    return g_current;
}

void catalogUnlock(void)
{
    mutexUnlock(&g_lock);
}

bool catalogScanProgress(size_t *done, size_t *total)
{
    mutexLock(&g_progress_lock);
    bool busy = g_scanning;
    *done = g_done;
    *total = g_total;
    mutexUnlock(&g_progress_lock);
    return busy;
}
