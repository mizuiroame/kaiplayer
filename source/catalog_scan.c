#include "catalog_scan.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "kfile.h"
#include "lang.h"
#include "library.h"
#include "tracklist.h"
#include "util.h"

#define THREAD_STACK_SIZE 0x40000  // Roomy: a network folder's reads (libsmb2, curl) run on it.
#define THREAD_PRIORITY 0x32  // Below the artwork and list-tag threads.
#define CACHE_NAME "catalog.txt"
#define NETWORK_CACHE_NAME "catalog-network.txt"
#define NETWORK_FAILURES_MAX 3  // Songs in a row whose tags can't be read: the server has gone.
#define PROBLEM_MAX 192

static Mutex g_lock;
static CondVar g_wake;
static Thread g_thread;
static bool g_started, g_running;
static char g_root[UTIL_PATH_MAX];  // Requested root.
static bool g_pending;
static bool g_fresh;                // Forget what was kept for g_root first.
static Catalog *g_current;          // Published; read under g_lock.
static Mutex g_progress_lock;     // Separate: read while the catalog is locked.
static bool g_scanning;
static size_t g_done, g_total;
static unsigned g_generation;   // Bumped by each publish.
static char g_problem[PROBLEM_MAX]; // Under g_progress_lock.

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

// catalog.txt in KaiPlayer's folder; the network folder's in a file of its own.
static void cachePath(const char *root, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", configFolder(), kfileIsRemote(root) ? NETWORK_CACHE_NAME : CACHE_NAME);
}

static void setProblem(const char *text)
{
    mutexLock(&g_progress_lock);
    snprintf(g_problem, sizeof(g_problem), "%s", text);
    mutexUnlock(&g_progress_lock);
}

void catalogScanProblem(char *out, size_t size)
{
    mutexLock(&g_progress_lock);
    snprintf(out, size, "%s", g_problem);
    mutexUnlock(&g_progress_lock);
}

void catalogScanDropCache(const char *root)
{
    char path[UTIL_PATH_MAX];
    cachePath(root, path, sizeof(path));
    remove(path);
}

// The saved catalog, keeping only songs inside root. NULL if there is none.
static Catalog *loadCache(const char *root)
{
    char path[UTIL_PATH_MAX];
    cachePath(root, path, sizeof(path));
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

static void saveCache(const char *root, const Catalog *c)
{
    char path[UTIL_PATH_MAX], temp[UTIL_PATH_MAX + 8];
    cachePath(root, path, sizeof(path));
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

// A network folder's song: from the previous catalog if its size and time
// are the same, else from its tags, read over the network. *failed: they
// couldn't be read (it's kept without them, to be read again next time).
static bool addNetworkFile(Catalog *fresh, const Catalog *previous, const LibraryFile *file, const char *root,
                           TrackTags *tags, bool *failed)
{
    long known = previous ? catalogFind(previous, file->path) : -1;
    *failed = false;
    if (known >= 0 && previous->songs[known].size == (long)file->size &&
        previous->songs[known].modified == file->modified)
        return catalogAddFields(fresh, &previous->songs[known]);
    tagsInit(tags);
    tags->skip_picture = true;
    *failed = !tagsRead(file->path, tags);
    bool ok;
    if (*failed && known >= 0)  // What it was: better than nothing until it can be read.
        ok = catalogAddFields(fresh, &previous->songs[known]);
    else  // Unread: a size that never matches, so the next scan reads it.
        ok = catalogAddFile(fresh, file->path, *failed ? -1 : (long)file->size, file->modified, tags, root);
    tagsFree(tags);
    return ok;
}

static Catalog *newCatalog(void)
{
    Catalog *c = malloc(sizeof(Catalog));
    if (c)
        catalogInit(c);
    return c;
}

static Catalog *finish(Catalog *fresh, bool ok)
{
    ok = ok && fresh && catalogBuild(fresh);
    if (!ok && fresh) {
        catalogFree(fresh);
        free(fresh);
        return NULL;
    }
    return fresh;
}

// Scans a network folder into a new catalog: its listings, then the tags of
// what's new. NULL if it was cancelled, failed, or the server went.
static Catalog *scanNetwork(const char *root, const Catalog *previous)
{
    LibraryFiles files;
    char why[PROBLEM_MAX] = "";
    if (!libraryScanNetwork(&files, root, restartRequested, why, sizeof(why))) {
        if (!restartRequested())
            setProblem(why[0] ? why : T("The network folder couldn't be read."));
        return NULL;
    }
    mutexLock(&g_progress_lock);
    g_total = files.count;
    mutexUnlock(&g_progress_lock);
    Catalog *fresh = newCatalog();
    TrackTags *tags = malloc(sizeof(TrackTags));
    bool ok = fresh && tags;
    int failures = 0;  // In a row.
    for (size_t i = 0; i < files.count && ok; i++) {
        bool failed = false;
        ok = !restartRequested() && addNetworkFile(fresh, previous, &files.files[i], root, tags, &failed);
        failures = failed ? failures + 1 : 0;
        if (ok && failures >= NETWORK_FAILURES_MAX) {
            // What was read is kept, and the rest as it was found before; the next scan goes on.
            setProblem(T("The network folder stopped answering. What was found before is kept."));
            for (size_t j = i + 1; j < files.count && ok; j++) {
                long known = previous ? catalogFind(previous, files.files[j].path) : -1;
                if (known >= 0)
                    ok = catalogAddFields(fresh, &previous->songs[known]);
            }
            break;
        }
        mutexLock(&g_progress_lock);
        g_done = i + 1;
        mutexUnlock(&g_progress_lock);
    }
    libraryFilesFree(&files);
    free(tags);
    return finish(fresh, ok);
}

// Scans root into a new catalog. NULL if it was cancelled or failed.
static Catalog *scan(const char *root, const Catalog *previous)
{
    if (kfileIsRemote(root))
        return scanNetwork(root, previous);
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
        bool fresh_start = g_fresh;
        g_pending = g_fresh = false;
        mutexUnlock(&g_lock);
        mutexLock(&g_progress_lock);
        g_scanning = true;
        g_done = g_total = 0;
        g_problem[0] = '\0';
        mutexUnlock(&g_progress_lock);

        if (fresh_start) {  // What was kept belongs to another network folder.
            catalogScanDropCache(root);
            scanned_root[0] = '\0';
        }
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
            saveCache(root, fresh);
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

static void start(const char *root, bool fresh)
{
    if (!g_started)
        return;
    mutexLock(&g_lock);
    snprintf(g_root, sizeof(g_root), "%s", root);
    g_pending = true;
    g_fresh = g_fresh || fresh;
    condvarWakeOne(&g_wake);
    mutexUnlock(&g_lock);
}

void catalogScanStart(const char *root)
{
    start(root, false);
}

void catalogScanStartFresh(const char *root)
{
    start(root, true);
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
