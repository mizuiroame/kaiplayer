#include "streamfile.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cover.h"
#include "lang.h"
#include "net.h"
#include "stream.h"

#define HTTP_PARTIAL 206
#define READ_STALL_S 8L     // The Archive is on the internet, not the home network.
#define COVER_STALL_S 8L
#define COVER_TIMEOUT_S 20L

typedef struct {
    NetClient *net;           // A stream's own client, or NULL for the shared one.
    char url[STREAM_URL_MAX];
    uint8_t *cover;           // A cover, read whole when opened,
    size_t cover_size;        // and its length.
} StreamFile;

static bool g_enabled = true;  // Settings' switch: off, nothing connects to archive.org.

void streamFileSetEnabled(bool on)
{
    __atomic_store_n(&g_enabled, on, __ATOMIC_RELEASE);
}

bool streamFileEnabled(void)
{
    return __atomic_load_n(&g_enabled, __ATOMIC_ACQUIRE);
}

// Small reads (tags, the start of a song) share one client, which keeps its connection.
static pthread_mutex_t g_peek_lock = PTHREAD_MUTEX_INITIALIZER;
static NetClient *g_peek;

void streamFileForget(void)
{
    pthread_mutex_lock(&g_peek_lock);
    netClientClose(g_peek);
    g_peek = NULL;
    pthread_mutex_unlock(&g_peek_lock);
}

typedef struct {
    uint8_t *to;
    size_t size, room;
} Filling;

static bool fill(const void *data, size_t size, void *user)
{
    Filling *f = user;
    size_t n = size < f->room - f->size ? size : f->room - f->size;
    memcpy(f->to + f->size, data, n);
    f->size += n;
    return n == size;
}

// Asks for bytes [from, from + size) into to. How many came; -1 if the answer wasn't those bytes.
static int64_t fetchRange(StreamFile *f, int64_t from, uint8_t *to, size_t size, long long *total,
                          const volatile bool *cancel)
{
    char range[64], error[NET_ERROR_MAX];
    snprintf(range, sizeof(range), "%" PRId64 "-%" PRId64, from, from + (int64_t)size - 1);
    NetRequest request = {.url = f->url, .range = range, .cancel = cancel, .total_size = total,
                          .stall_s = READ_STALL_S};
    Filling filling = {to, 0, size};
    long status = 0;
    *total = -1;
    bool shared = !f->net;
    if (shared) {
        pthread_mutex_lock(&g_peek_lock);
        if (!g_peek)
            g_peek = netClientOpen();
    }
    NetClient *client = shared ? g_peek : f->net;
    bool ok = client && netFetch(client, &request, fill, &filling, &status, error, sizeof(error));
    if (shared)
        pthread_mutex_unlock(&g_peek_lock);
    return ok && status == HTTP_PARTIAL ? (int64_t)filling.size : -1;
}

static void freeFile(StreamFile *f)
{
    if (!f)
        return;
    netClientClose(f->net);
    free(f->cover);
    free(f);
}

// A cover, whole: an image (an error page isn't one).
static bool openCover(StreamFile *f, const StreamTarget *t, int64_t *size)
{
    if (!streamCoverUrl(t, f->url, sizeof(f->url)))
        return false;
    NetClient *c = netClientOpen();
    NetRequest request = {.url = f->url, .stall_s = COVER_STALL_S, .timeout_s = COVER_TIMEOUT_S};
    NetResponse response;
    bool ok = c && netFetchText(c, &request, COVER_FILE_MAX, &response);
    netClientClose(c);
    if (!ok)
        return false;
    ok = response.size > 0 && response.data[0] != '{' && response.data[0] != '<';
    if (ok) {
        f->cover = (uint8_t *)response.data;  // Taken: not freed here.
        f->cover_size = response.size;
        *size = (int64_t)response.size;
    } else {
        netResponseFree(&response);
    }
    return ok;
}

// A song: its address, then its length from a range's answer.
static bool openSong(StreamFile *f, const StreamTarget *t, bool stream, int64_t *size)
{
    if (stream && !(f->net = netClientOpen()))
        return false;
    if (!streamFileUrl(t, f->url, sizeof(f->url)))
        return false;
    uint8_t first;
    long long total = -1;
    if (fetchRange(f, 0, &first, 1, &total, NULL) == 1 && total > 0) {
        *size = total;
        return true;
    }
    return false;
}

static void *openFile(const char *location, bool stream, int64_t *size)
{
    if (!streamFileEnabled())
        return NULL;  // Turned off in Settings: player.c says so.
    StreamTarget *target = malloc(sizeof(*target));
    StreamFile *f = calloc(1, sizeof(*f));
    bool ok = target && f && streamParseLocation(location, target) &&
              (target->cover ? openCover(f, target, size) : openSong(f, target, stream, size));
    free(target);
    if (!ok) {
        freeFile(f);
        return NULL;
    }
    return f;
}

static int64_t readFile(void *file, int64_t offset, void *to, size_t size, const volatile bool *cancel)
{
    StreamFile *f = file;
    if (f->cover) {
        if (offset < 0 || (uint64_t)offset > f->cover_size)
            return -1;
        size_t n = f->cover_size - (size_t)offset < size ? f->cover_size - (size_t)offset : size;
        memcpy(to, f->cover + offset, n);
        return (int64_t)n;
    }
    long long total;
    return fetchRange(f, offset, to, size, &total, cancel);
}

static void closeFile(void *file)
{
    freeFile(file);
}

// The Internet Archive is browsed in the Library's own pages: it has no folders.
static bool listFolder(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    (void)location;
    *out = NULL;
    *count = 0;
    snprintf(why, why_size, "%s", T("The Internet Archive has no folders here: open it in Library."));
    return false;
}

static const KFileRemote REMOTE = {openFile, readFile, closeFile, listFolder};

const KFileRemote *streamFileRemote(void)
{
    return &REMOTE;
}
