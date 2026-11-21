#include "catalog_scan.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "dav.h"
#include "kfile.h"
#include "lang.h"
#include "library.h"
#include "subnet.h"
#include "tracklist.h"
#include "util.h"

#define THREAD_STACK_SIZE 0x40000  // Roomy: a network folder's reads (libsmb2, curl) run on it.
#define THREAD_PRIORITY 0x32  // Below the artwork and list-tag threads.
#define CACHE_NAME "catalog.txt"
#define NETWORK_CACHE_NAME "catalog-network.txt"
#define SERVER_CACHE_NAME "catalog-subsonic.txt"
#define NETWORK_FAILURES_MAX 3  // Songs in a row whose tags can't be read: the server has gone.
#define PROBLEM_MAX 192

static Mutex g_lock;
static CondVar g_wake;
static Thread g_thread;
static bool g_started, g_running;
static char g_root[UTIL_PATH_MAX];  // Requested root.
static bool g_pending;
static bool g_fresh;                // Forget what was kept for g_root first.

// The libraries on the network: the network folder's and the Navidrome server's.
enum { REMOTE_FOLDER, REMOTE_SERVER, REMOTES };
static const char *const REMOTE_ROOTS[REMOTES] = {DAV_ROOT, SUB_ROOT};

// Which of them root is; -1 for the card.
static int remoteOf(const char *root)
{
    return davIsLocation(root) ? REMOTE_FOLDER : subIsLocation(root) ? REMOTE_SERVER : -1;
}

// A library on the network, read in the background while another is shown,
// into its cache file only. The shown library's scan goes first.
static bool g_background[REMOTES];        // Asked for.
static bool g_background_fresh[REMOTES];  // Forget what was kept first (another address).
static bool g_forget[REMOTES];            // Forget what was kept, and don't read it (not set up now).
static bool g_in_background;        // The thread is doing one now.
static int g_in_remote = -1;        // The scan running is this one's (shown or not); -1: the card's, or none.
static Catalog *g_current;          // Published; read under g_lock.
static Mutex g_progress_lock;     // Separate: read while the catalog is locked.
static bool g_scanning;
static size_t g_done, g_total;
static bool g_net_scanning;         // A library on the network being read, shown or not,
static int g_net_remote;            // which one,
static size_t g_net_done, g_net_total;  // and its songs read so far.
static unsigned g_generation;   // Bumped by each publish.
static char g_problem[REMOTES][PROBLEM_MAX]; // Under g_progress_lock.

// The scan running should stop: another is asked for (a background one gives
// way to the shown library's, and to a new request of its own), or the end.
static bool restartRequested(void)
{
    mutexLock(&g_lock);
    bool pending = g_pending || !g_running ||
                   (g_in_remote >= 0 && (g_forget[g_in_remote] || (g_in_background && g_background[g_in_remote])));
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

// catalog.txt in KaiPlayer's folder; the network folder's and the server's in files of their own.
static void cachePath(const char *root, char *out, size_t size)
{
    int remote = remoteOf(root);
    const char *name = remote == REMOTE_FOLDER ? NETWORK_CACHE_NAME : remote == REMOTE_SERVER ? SERVER_CACHE_NAME
                                                                                              : CACHE_NAME;
    snprintf(out, size, "%s/%s", configFolder(), name);
}

static void setProblem(int remote, const char *text)
{
    mutexLock(&g_progress_lock);
    snprintf(g_problem[remote], sizeof(g_problem[remote]), "%s", text);
    mutexUnlock(&g_progress_lock);
}

void catalogScanProblem(const char *root, char *out, size_t size)
{
    int remote = remoteOf(root);
    mutexLock(&g_progress_lock);
    snprintf(out, size, "%s", remote >= 0 ? g_problem[remote] : "");
    mutexUnlock(&g_progress_lock);
}

// Only on the scan's thread, which is the only one that writes them.
static void dropCache(const char *root)
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

static void setProgress(bool shown, size_t done, size_t total)
{
    mutexLock(&g_progress_lock);
    if (shown) {
        g_done = done;
        g_total = total;
    }
    g_net_done = done;
    g_net_total = total;
    mutexUnlock(&g_progress_lock);
}

// The songs from files[from] on as they were found before (when the scan stops part way).
static bool keepRest(Catalog *fresh, const Catalog *previous, const LibraryFiles *files, size_t from)
{
    bool ok = true;
    for (size_t j = from; j < files->count && ok; j++) {
        long known = previous ? catalogFind(previous, files->files[j].path) : -1;
        if (known >= 0)
            ok = catalogAddFields(fresh, &previous->songs[known]);
    }
    return ok;
}

// Scans a network folder into a new catalog: its listings, then the tags of
// what's new. shown: it's the library on screen (its progress shows in
// Library). NULL if its listing failed or was stopped. Stopped while reading
// tags, or when the server stops answering, it still gives what was read
// (and the rest as found before), with *partial set: worth keeping.
static Catalog *scanNetwork(const char *root, const Catalog *previous, bool shown, bool *partial)
{
    *partial = false;
    LibraryFiles files;
    char why[PROBLEM_MAX] = "";
    if (!libraryScanNetwork(&files, root, restartRequested, why, sizeof(why))) {
        if (!restartRequested())
            setProblem(REMOTE_FOLDER, why[0] ? why : T("The network folder couldn't be read."));
        return NULL;
    }
    setProgress(shown, 0, files.count);
    Catalog *fresh = newCatalog();
    TrackTags *tags = malloc(sizeof(TrackTags));
    bool ok = fresh && tags;
    int failures = 0;  // In a row.
    for (size_t i = 0; i < files.count && ok; i++) {
        if (restartRequested()) {
            *partial = true;
            ok = keepRest(fresh, previous, &files, i);
            break;
        }
        bool failed = false;
        ok = addNetworkFile(fresh, previous, &files.files[i], root, tags, &failed);
        failures = failed ? failures + 1 : 0;
        if (ok && failures >= NETWORK_FAILURES_MAX) {
            // What was read is kept, and the rest as it was found before; the next scan goes on.
            setProblem(REMOTE_FOLDER, T("The network folder stopped answering. What was found before is kept."));
            *partial = true;
            ok = keepRest(fresh, previous, &files, i + 1);
            break;
        }
        setProgress(shown, i + 1, files.count);
    }
    libraryFilesFree(&files);
    free(tags);
    return finish(fresh, ok);
}

static void serverProgress(size_t done, size_t total, void *user)
{
    setProgress(*(const bool *)user, done, total);
}

// The Navidrome server's library, from its answers (no file is read). NULL if
// it couldn't be read, or was stopped: then *partial is set, to read it again later.
static Catalog *scanServer(bool shown, bool *partial)
{
    *partial = false;
    setProgress(shown, 0, 0);
    Catalog *fresh = newCatalog();
    char why[PROBLEM_MAX] = "";
    if (fresh && subFetchLibrary(fresh, restartRequested, serverProgress, &shown, why, sizeof(why)))
        return finish(fresh, true);
    if (restartRequested())
        *partial = true;
    else
        setProblem(REMOTE_SERVER, why[0] ? why : T("The Navidrome server's library couldn't be read."));
    finish(fresh, false);
    return NULL;
}

// Scans root into a new catalog. NULL if it was cancelled or failed.
static Catalog *scan(const char *root, const Catalog *previous, bool *partial)
{
    *partial = false;
    if (remoteOf(root) == REMOTE_SERVER)
        return scanServer(true, partial);
    if (kfileIsRemote(root))
        return scanNetwork(root, previous, true, partial);
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

// remote: the library on the network being scanned, or -1 for the card's.
static void setScanning(bool shown, int remote, bool on)
{
    mutexLock(&g_lock);
    g_in_remote = on ? remote : -1;
    mutexUnlock(&g_lock);
    mutexLock(&g_progress_lock);
    if (shown)
        g_scanning = on;
    if (remote >= 0) {
        g_net_scanning = on;
        g_net_remote = remote;
    }
    if (on && shown)
        g_done = g_total = 0;
    if (on && remote >= 0) {
        g_net_done = g_net_total = 0;
        g_problem[remote][0] = '\0';
    }
    mutexUnlock(&g_progress_lock);
}

static char g_scanned_root[UTIL_PATH_MAX];  // The folder g_current belongs to (this thread's).

// The shown library's scan: its cache shows at once, then the new catalog.
static void scanShown(const char *root, bool fresh_start)
{
    int remote = remoteOf(root);
    setScanning(true, remote, true);
    if (fresh_start) {  // What was kept belongs to another network folder.
        dropCache(root);
        g_scanned_root[0] = '\0';
    }
    // Another folder: its catalog starts over. Only this thread replaces
    // or frees g_current, so it can also read it unlocked below.
    if (strcmp(root, g_scanned_root) != 0) {
        publish(NULL);
        snprintf(g_scanned_root, sizeof(g_scanned_root), "%s", root);
    }
    if (!g_current) {
        Catalog *cached = loadCache(root);  // Shown while the scan runs.
        if (cached)
            publish(cached);
    }
    bool partial;
    Catalog *fresh = scan(root, g_current, &partial);
    if (fresh) {
        saveCache(root, fresh);  // Even stopped part way: what was read isn't read again.
        mutexLock(&g_lock);
        bool kept = !partial || !g_pending || strcmp(g_root, root) == 0;  // Not if another folder comes next.
        mutexUnlock(&g_lock);
        if (kept)
            publish(fresh);
        else {
            catalogFree(fresh);
            free(fresh);
        }
    }
    mutexLock(&g_lock);
    if (partial && remote >= 0 && (!g_pending || strcmp(g_root, root) != 0))
        g_background[remote] = true;  // Left for another: finished in the background after it.
    mutexUnlock(&g_lock);
    setScanning(true, remote, false);
}

// A library on the network while another is shown: into its cache only.
static void scanBackground(int remote, bool fresh_start)
{
    const char *root = REMOTE_ROOTS[remote];
    if (fresh_start)
        dropCache(root);
    setScanning(false, remote, true);
    Catalog *previous = remote == REMOTE_FOLDER ? loadCache(root) : NULL;  // The server's isn't needed.
    bool partial;
    Catalog *fresh = remote == REMOTE_FOLDER ? scanNetwork(root, previous, false, &partial)
                                             : scanServer(false, &partial);
    if (fresh) {
        saveCache(root, fresh);
        catalogFree(fresh);
        free(fresh);
    }
    if (previous) {
        catalogFree(previous);
        free(previous);
    }
    mutexLock(&g_lock);
    g_in_background = false;
    // Stopped for the shown library's scan: finished after it (unless that one is this library's).
    if (partial && g_running && !g_forget[remote] && !(g_pending && remoteOf(g_root) == remote))
        g_background[remote] = true;
    mutexUnlock(&g_lock);
    setScanning(false, remote, false);
}

// The first remote whose flag is set, or -1.
static int firstSet(const bool flags[REMOTES])
{
    for (int i = 0; i < REMOTES; i++)
        if (flags[i])
            return i;
    return -1;
}

static void workerMain(void *arg)
{
    (void)arg;
    char root[UTIL_PATH_MAX];
    mutexLock(&g_lock);
    while (g_running) {
        int remote = firstSet(g_forget);
        if (remote >= 0) {  // Not set up now: what was kept of it goes.
            g_forget[remote] = g_background[remote] = g_background_fresh[remote] = false;
            mutexUnlock(&g_lock);
            dropCache(REMOTE_ROOTS[remote]);
            if (remoteOf(g_scanned_root) == remote) {
                publish(NULL);
                g_scanned_root[0] = '\0';
            }
            mutexLock(&g_lock);
            continue;
        }
        if (g_pending) {
            snprintf(root, sizeof(root), "%s", g_root);
            bool fresh_start = g_fresh;
            g_pending = g_fresh = false;
            remote = remoteOf(root);
            if (remote >= 0) {  // The shown scan is that library's: no background one too.
                fresh_start = fresh_start || g_background_fresh[remote];
                g_background[remote] = g_background_fresh[remote] = false;
            }
            mutexUnlock(&g_lock);
            scanShown(root, fresh_start);
            mutexLock(&g_lock);
            continue;
        }
        remote = firstSet(g_background);
        if (remote >= 0) {
            bool fresh_start = g_background_fresh[remote];
            g_background[remote] = g_background_fresh[remote] = false;
            if (remoteOf(g_scanned_root) == remote) {  // It's the library shown: its scan, shown.
                mutexUnlock(&g_lock);
                scanShown(REMOTE_ROOTS[remote], fresh_start);
            } else {
                g_in_background = true;
                mutexUnlock(&g_lock);
                scanBackground(remote, fresh_start);
            }
            mutexLock(&g_lock);
            continue;
        }
        condvarWait(&g_wake, &g_lock);
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

void catalogScanRefreshRemote(const char *root, bool fresh)
{
    int remote = remoteOf(root);
    if (!g_started || remote < 0)
        return;
    mutexLock(&g_lock);
    g_background[remote] = true;
    g_background_fresh[remote] = g_background_fresh[remote] || fresh;
    condvarWakeOne(&g_wake);
    mutexUnlock(&g_lock);
}

void catalogScanForgetRemote(const char *root)
{
    int remote = remoteOf(root);
    if (!g_started || remote < 0)
        return;
    mutexLock(&g_lock);
    g_forget[remote] = true;
    condvarWakeOne(&g_wake);
    mutexUnlock(&g_lock);
}

bool catalogScanNetworkProgress(size_t *done, size_t *total, const char **root)
{
    mutexLock(&g_progress_lock);
    bool busy = g_net_scanning;
    *done = g_net_done;
    *total = g_net_total;
    if (root)
        *root = REMOTE_ROOTS[g_net_remote];
    mutexUnlock(&g_progress_lock);
    return busy;
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
