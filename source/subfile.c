#include "subfile.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cover.h"
#include "lang.h"
#include "net.h"
#include "subnet.h"

#define URL_MAX 2048
#define HTTP_PARTIAL 206
#define READ_STALL_S 5L
#define COVER_STALL_S 8L
#define COVER_TIMEOUT_S 20L

typedef struct {
    NetClient *net;           // A stream's own client, or NULL for the shared one.
    char url[URL_MAX];        // Holds the sign-in's token: cleared before it's freed.
    bool any_certificate;
    uint8_t *cover;           // A cover, read whole when opened,
    size_t cover_size;        // and its length.
} SubFile;

// Small reads (tags, the start of a song) share one client, which keeps its connection.
static pthread_mutex_t g_peek_lock = PTHREAD_MUTEX_INITIALIZER;
static NetClient *g_peek;

void subFileForget(void)
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
static int64_t fetchRange(SubFile *f, int64_t from, uint8_t *to, size_t size, long long *total,
                          const volatile bool *cancel)
{
    char range[64], error[NET_ERROR_MAX];
    snprintf(range, sizeof(range), "%" PRId64 "-%" PRId64, from, from + (int64_t)size - 1);
    NetRequest request = {.url = f->url, .range = range, .cancel = cancel, .total_size = total,
                          .stall_s = READ_STALL_S, .any_certificate = f->any_certificate};
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

static void freeFile(SubFile *f)
{
    if (!f)
        return;
    netClientClose(f->net);
    memset(f->url, 0, sizeof(f->url));
    free(f->cover);
    free(f);
}

// A cover, whole: an image (a JSON error isn't one).
static bool openCover(SubFile *f, const char *location, int64_t *size)
{
    if (!subCoverUrl(location, f->url, sizeof(f->url), &f->any_certificate))
        return false;
    NetClient *c = netClientOpen();
    NetRequest request = {.url = f->url, .stall_s = COVER_STALL_S, .timeout_s = COVER_TIMEOUT_S,
                          .any_certificate = f->any_certificate};
    NetResponse response;
    bool ok = c && netFetchText(c, &request, COVER_FILE_MAX, &response);
    netClientClose(c);
    if (!ok)
        return false;
    ok = response.size > 0 && response.data[0] != '{';
    if (ok) {
        f->cover = (uint8_t *)response.data;  // Taken: not freed here.
        f->cover_size = response.size;
        *size = (int64_t)response.size;
    } else {
        netResponseFree(&response);
    }
    return ok;
}

// A song: its length, from a range's answer. "download" first; "stream" as it is if downloads are off.
static bool openSong(SubFile *f, const char *location, bool stream, int64_t *size)
{
    if (stream && !(f->net = netClientOpen()))
        return false;
    for (int raw = 0; raw < 2; raw++) {
        if (!subSongUrl(location, raw, f->url, sizeof(f->url), &f->any_certificate))
            return false;
        uint8_t first;
        long long total = -1;
        if (fetchRange(f, 0, &first, 1, &total, NULL) == 1 && total >= 0) {
            *size = total;
            return true;
        }
    }
    return false;
}

static void *openFile(const char *location, bool stream, int64_t *size)
{
    SubTarget target;
    if (!subParseLocation(location, &target))
        return NULL;
    SubFile *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    bool ok = target.cover ? openCover(f, location, size) : openSong(f, location, stream, size);
    if (!ok) {
        freeFile(f);
        return NULL;
    }
    return f;
}

static int64_t readFile(void *file, int64_t offset, void *to, size_t size, const volatile bool *cancel)
{
    SubFile *f = file;
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

// The server's songs are in Artists, Albums and Songs: it has no folders to show.
static bool listFolder(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    (void)location;
    *out = NULL;
    *count = 0;
    snprintf(why, why_size, "%s", T("Navidrome's songs are in Artists, Albums and Songs: it has no folders here."));
    return false;
}

static const KFileRemote REMOTE = {openFile, readFile, closeFile, listFolder};

const KFileRemote *subFileRemote(void)
{
    return &REMOTE;
}
