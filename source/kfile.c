#include "kfile.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "blockcache.h"
#include "stream.h"
#include "subsonic.h"

// A song playing: blocks of 256 KB, 4 MB read ahead (about 30 s of FLAC),
// some kept behind for a short seek back.
#define STREAM_BLOCK (256 * 1024)
#define STREAM_SLOTS 24
#define STREAM_AHEAD 16
#define STREAM_TRIES 3        // With the server's own waits: about 20 s without it, then it's lost.
// Tags and covers: a few small blocks, read when asked for.
#define PEEK_BLOCK (64 * 1024)
#define PEEK_SLOTS 4
#define PEEK_TRIES 1          // The remote makes a lost connection again by itself.
#define MAX_SLOTS STREAM_SLOTS
#define FETCH_STACK (256 * 1024)  // curl and libsmb2 run on it.

#define RETRY_WAIT_MS 300     // Doubles with each try.
#define WAIT_STEP_MS 50
#define NS_PER_MS 1000000L
#define NS_PER_S 1000000000L

static size_t g_stream_block = STREAM_BLOCK, g_peek_block = PEEK_BLOCK;
static const KFileRemote *g_remotes[KFILE_REMOTE_COUNT];
static unsigned g_stream_interrupts;  // Streams opened before the last interrupt give up.

typedef struct {
    const KFileRemote *ops;
    void *file;                // The remote's.
    bool stream;
    int64_t total;             // The file's length.
    size_t block;              // Bytes per block.
    int slot_count, ahead, tries;
    BlockSlot slots[MAX_SLOTS];
    uint8_t *memory;           // slot_count blocks.
    // A stream's thread, which fetches ahead of the reader. The rest is under lock.
    pthread_t thread;
    bool synced, thread_started;  // lock and wake made; the thread running.
    pthread_mutex_t lock;
    pthread_cond_t wake;       // The reader moved, a block came, or stop.
    int64_t reader;            // The block the reader is at.
    int64_t fetching;          // The block on its way, or -1.
    volatile bool abort_fetch; // The reader went elsewhere, or stop.
    bool stop, failed;
    unsigned interrupts;       // g_stream_interrupts when it was opened.
} Remote;

struct KFile {
    FILE *file;      // A card file,
    Remote *remote;  // or a network one.
    int64_t position;
    bool failed;
};

void kfileSetRemote(KFileRemoteKind kind, const KFileRemote *remote)
{
    if (kind >= 0 && kind < KFILE_REMOTE_COUNT)
        __atomic_store_n(&g_remotes[kind], remote, __ATOMIC_RELEASE);
}

// What reads a location: the network folder's ("dav:/"), the server's ("sub:/") or the Archive's ("ia:/"). NULL for none.
static const KFileRemote *remoteOps(const char *location)
{
    KFileRemoteKind kind = davIsLocation(location) ? KFILE_REMOTE_FOLDER
                         : subIsLocation(location) ? KFILE_REMOTE_SERVER
                         : streamIsLocation(location) ? KFILE_REMOTE_STREAM
                                                   : KFILE_REMOTE_COUNT;
    return kind == KFILE_REMOTE_COUNT ? NULL : __atomic_load_n(&g_remotes[kind], __ATOMIC_ACQUIRE);
}

bool kfileIsRemote(const char *path)
{
    return davIsLocation(path) || subIsLocation(path) || streamIsLocation(path);
}

void kfileSetBlockSizes(size_t stream, size_t peek)
{
    g_stream_block = stream ? stream : STREAM_BLOCK;
    g_peek_block = peek ? peek : PEEK_BLOCK;
}

void kfileInterruptStreams(void)
{
    __atomic_add_fetch(&g_stream_interrupts, 1, __ATOMIC_RELAXED);
}

// --- Fetching a block ---

static void sleepMs(int ms)
{
    nanosleep(&(struct timespec){ms / 1000, (long)(ms % 1000) * NS_PER_MS}, NULL);
}

// Waits, stopping early if the fetch is called off.
static void waitBeforeRetry(const volatile bool *abort, int ms)
{
    for (int waited = 0; waited < ms && !(abort && *abort); waited += WAIT_STEP_MS)
        sleepMs(WAIT_STEP_MS);
}

static int64_t totalBlocks(const Remote *r)
{
    return (r->total + (int64_t)r->block - 1) / (int64_t)r->block;
}

// Reads block index into to, whole. Its size, or 0 if it couldn't be read.
static size_t fetchBlock(Remote *r, int64_t index, uint8_t *to, const volatile bool *abort)
{
    int64_t from = index * (int64_t)r->block;
    if (from >= r->total)
        return 0;
    size_t want = r->total - from < (int64_t)r->block ? (size_t)(r->total - from) : r->block;
    for (int attempt = 0; attempt < r->tries && !(abort && *abort); attempt++) {
        if (attempt)
            waitBeforeRetry(abort, RETRY_WAIT_MS << (attempt - 1));
        if (r->ops->read(r->file, from, to, want, abort) == (int64_t)want)
            return want;
    }
    return 0;
}

// --- A stream: the thread that fetches ahead ---

// Waits for a block to fetch and a slot for it. False to stop. Under the lock.
static bool nextJob(Remote *r, int64_t *index, int *slot)
{
    while (!r->stop) {
        *index = r->failed ? -1 : blockNextWanted(r->slots, r->slot_count, &r->reader, 1, totalBlocks(r), r->ahead);
        *slot = *index >= 0 ? blockVictim(r->slots, r->slot_count, &r->reader, 1, r->ahead) : -1;
        if (*slot >= 0)
            return true;
        pthread_cond_wait(&r->wake, &r->lock);
    }
    return false;
}

static void *fetchThread(void *arg)
{
    Remote *r = arg;
    pthread_mutex_lock(&r->lock);
    int64_t index;
    int slot;
    while (nextJob(r, &index, &slot)) {
        r->slots[slot] = (BlockSlot){.index = index, .state = BLOCK_LOADING};
        r->fetching = index;
        r->abort_fetch = false;
        pthread_mutex_unlock(&r->lock);
        size_t size = fetchBlock(r, index, r->memory + (size_t)slot * r->block, &r->abort_fetch);
        pthread_mutex_lock(&r->lock);
        r->fetching = -1;
        r->slots[slot].state = size ? BLOCK_READY : BLOCK_EMPTY;
        r->slots[slot].size = size;
        if (!size && !r->abort_fetch)
            r->failed = true;
        pthread_cond_broadcast(&r->wake);
    }
    pthread_mutex_unlock(&r->lock);
    return NULL;
}

static bool interrupted(const Remote *r)
{
    return __atomic_load_n(&g_stream_interrupts, __ATOMIC_RELAXED) != r->interrupts;
}

// The reader is at this block now: what's being fetched elsewhere can stop. Under the lock.
static void moveReader(Remote *r, int64_t block)
{
    if (block == r->reader)
        return;
    r->reader = block;
    r->failed = false;
    if (r->fetching >= 0 && !blockIsWanted(r->fetching, &r->reader, 1, r->ahead))
        r->abort_fetch = true;
    pthread_cond_broadcast(&r->wake);
}

// The ready slot of a block, waiting for it. -1 if it can't come. Under the lock.
static int waitForBlock(Remote *r, int64_t block)
{
    for (;;) {
        int slot = blockFind(r->slots, r->slot_count, block);
        if (slot >= 0 && r->slots[slot].state == BLOCK_READY)
            return slot;
        if (r->stop || r->failed)
            return -1;
        if (interrupted(r)) {  // For good: its owner is moving on, the thread can stop fetching.
            r->stop = true;
            r->abort_fetch = true;
            pthread_cond_broadcast(&r->wake);
            return -1;
        }
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += WAIT_STEP_MS * NS_PER_MS;
        if (until.tv_nsec >= NS_PER_S) {
            until.tv_sec++;
            until.tv_nsec -= NS_PER_S;
        }
        pthread_cond_timedwait(&r->wake, &r->lock, &until);
    }
}

// --- Opening and reading ---

static void remoteFree(Remote *r)
{
    if (!r)
        return;
    if (r->thread_started) {
        pthread_mutex_lock(&r->lock);
        r->stop = true;
        r->abort_fetch = true;
        pthread_cond_broadcast(&r->wake);
        pthread_mutex_unlock(&r->lock);
        pthread_join(r->thread, NULL);
    }
    if (r->synced) {
        pthread_cond_destroy(&r->wake);
        pthread_mutex_destroy(&r->lock);
    }
    if (r->file)
        r->ops->close(r->file);
    free(r->memory);
    free(r);
}

// A stream starts fetching, and waits for its first block.
static bool startStream(Remote *r)
{
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->wake, NULL);
    r->synced = true;
    r->fetching = -1;
    r->interrupts = __atomic_load_n(&g_stream_interrupts, __ATOMIC_RELAXED);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, FETCH_STACK);
    r->thread_started = pthread_create(&r->thread, &attr, fetchThread, r) == 0;
    pthread_attr_destroy(&attr);
    if (!r->thread_started)
        return false;
    if (r->total == 0)
        return true;
    pthread_mutex_lock(&r->lock);
    bool ok = waitForBlock(r, 0) >= 0;
    pthread_mutex_unlock(&r->lock);
    return ok;
}

static Remote *remoteOpen(const char *location, KFileMode mode)
{
    const KFileRemote *ops = remoteOps(location);
    Remote *r = ops ? calloc(1, sizeof(*r)) : NULL;
    if (!r)
        return NULL;
    r->ops = ops;
    r->stream = mode == KFILE_STREAM;
    r->block = r->stream ? g_stream_block : g_peek_block;
    r->slot_count = r->stream ? STREAM_SLOTS : PEEK_SLOTS;
    r->ahead = r->stream ? STREAM_AHEAD : 1;
    r->tries = r->stream ? STREAM_TRIES : PEEK_TRIES;
    r->memory = malloc((size_t)r->slot_count * r->block);
    r->total = -1;
    r->file = r->memory ? ops->open(location, r->stream, &r->total) : NULL;
    bool ok = r->file && r->total >= 0 && (!r->stream || startStream(r));
    if (!ok) {
        remoteFree(r);
        return NULL;
    }
    return r;
}

static size_t streamRead(KFile *f, uint8_t *to, size_t size)
{
    Remote *r = f->remote;
    size_t done = 0;
    pthread_mutex_lock(&r->lock);
    while (done < size && f->position < r->total) {
        int64_t block = f->position / (int64_t)r->block;
        size_t offset = (size_t)(f->position % (int64_t)r->block);
        moveReader(r, block);
        int slot = waitForBlock(r, block);
        if (slot < 0 || offset >= r->slots[slot].size) {
            f->failed = true;
            break;
        }
        size_t n = r->slots[slot].size - offset;
        n = n < size - done ? n : size - done;
        memcpy(to + done, r->memory + (size_t)slot * r->block + offset, n);
        done += n;
        f->position += (int64_t)n;
    }
    pthread_mutex_unlock(&r->lock);
    return done;
}

// A peek's block, read now if it isn't here. -1 if it can't be.
static int peekBlock(Remote *r, int64_t block)
{
    int slot = blockFind(r->slots, r->slot_count, block);
    if (slot >= 0)
        return slot;
    slot = blockVictim(r->slots, r->slot_count, &block, 1, 1);
    if (slot < 0)
        return -1;
    size_t size = fetchBlock(r, block, r->memory + (size_t)slot * r->block, NULL);
    r->slots[slot] = (BlockSlot){.index = block, .state = size ? BLOCK_READY : BLOCK_EMPTY, .size = size};
    return size ? slot : -1;
}

static size_t peekRead(KFile *f, uint8_t *to, size_t size)
{
    Remote *r = f->remote;
    size_t done = 0;
    while (done < size && f->position < r->total) {
        int64_t block = f->position / (int64_t)r->block;
        size_t offset = (size_t)(f->position % (int64_t)r->block);
        int slot = peekBlock(r, block);
        if (slot < 0 || offset >= r->slots[slot].size) {
            f->failed = true;
            break;
        }
        size_t n = r->slots[slot].size - offset;
        n = n < size - done ? n : size - done;
        memcpy(to + done, r->memory + (size_t)slot * r->block + offset, n);
        done += n;
        f->position += (int64_t)n;
    }
    return done;
}

KFile *kfileOpen(const char *path, KFileMode mode)
{
    KFile *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    if (kfileIsRemote(path))
        f->remote = remoteOpen(path, mode);
    else
        f->file = fopen(path, "rb");
    if (!f->file && !f->remote) {
        free(f);
        return NULL;
    }
    return f;
}

void kfileClose(KFile *f)
{
    if (!f)
        return;
    if (f->file)
        fclose(f->file);
    remoteFree(f->remote);
    free(f);
}

size_t kfileRead(KFile *f, void *to, size_t size)
{
    if (f->file)
        return fread(to, 1, size, f->file);
    if (size == 0)
        return 0;
    return f->remote->stream ? streamRead(f, to, size) : peekRead(f, to, size);
}

bool kfileSeek(KFile *f, int64_t offset, int whence)
{
    if (f->file)
        return fseek(f->file, (long)offset, whence) == 0;
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
        return false;
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->position : f->remote->total;
    if (offset < -base)
        return false;
    f->position = base + offset;
    return true;
}

int64_t kfileTell(const KFile *f)
{
    return f->file ? (int64_t)ftell(f->file) : f->position;
}

int64_t kfileSize(KFile *f)
{
    if (!f->file)
        return f->remote->total;
    long here = ftell(f->file);
    if (here < 0 || fseek(f->file, 0, SEEK_END) != 0)
        return -1;
    long size = ftell(f->file);
    fseek(f->file, here, SEEK_SET);
    return size;
}

bool kfileFailed(const KFile *f)
{
    return f->failed;
}

FILE *kfileStdio(KFile *f)
{
    return f->file;
}

static uint8_t *readAllLocal(const char *path, size_t max, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *data = NULL;
    long length = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (length >= 0 && (unsigned long)length <= max && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc(length ? (size_t)length : 1);
        if (data && fread(data, 1, (size_t)length, f) != (size_t)length) {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    *size = data ? (size_t)length : 0;
    return data;
}

static uint8_t *readAllRemote(const char *path, size_t max, size_t *size, bool *failed)
{
    const KFileRemote *ops = remoteOps(path);
    int64_t total = -1;
    void *file = ops ? ops->open(path, false, &total) : NULL;
    if (!file) {
        *failed = true;
        return NULL;
    }
    uint8_t *data = total >= 0 && (uint64_t)total <= max ? malloc(total ? (size_t)total : 1) : NULL;
    if (data && total && ops->read(file, 0, data, (size_t)total, NULL) != total) {
        free(data);
        data = NULL;
        *failed = true;
    }
    ops->close(file);
    *size = data ? (size_t)total : 0;
    return data;
}

uint8_t *kfileReadAll(const char *path, size_t max, size_t *size, bool *failed)
{
    bool network = false;
    *size = 0;
    uint8_t *data = kfileIsRemote(path) ? readAllRemote(path, max, size, &network) : readAllLocal(path, max, size);
    if (failed)
        *failed = network;
    return data;
}

bool kfileList(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    const KFileRemote *ops = remoteOps(location);
    *out = NULL;
    *count = 0;
    if (!ops || !ops->list) {
        snprintf(why, why_size, "%s", "");
        return false;
    }
    return ops->list(location, out, count, why, why_size);
}
