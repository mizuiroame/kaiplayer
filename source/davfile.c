#include "davfile.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "davnet.h"
#include "net.h"
#include "smbnet.h"
#include "util.h"

#define URL_MAX 2048  // A network folder's paths, percent-encoded, run long.
#define AUTH_MAX 512
#define HTTP_PARTIAL 206
#define READ_STALL_S 5L

typedef struct {
    bool stream;
    char location[UTIL_PATH_MAX];
    SmbFile *smb;           // An SMB share's file (NULL after its connection failed: opened again),
    bool smb_share;
    NetClient *net;         // or WebDAV's: a stream's own client, or NULL for the shared one.
    char url[URL_MAX], auth[AUTH_MAX];
} DavFile;

// Small reads (tags, covers) over WebDAV share one client, which keeps its connection.
static pthread_mutex_t g_peek_lock = PTHREAD_MUTEX_INITIALIZER;
static NetClient *g_peek;

void davFileForget(void)
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
static int64_t fetchRange(DavFile *f, int64_t from, uint8_t *to, size_t size, long long *total,
                          const volatile bool *cancel)
{
    char range[64], error[NET_ERROR_MAX];
    snprintf(range, sizeof(range), "%" PRId64 "-%" PRId64, from, from + (int64_t)size - 1);
    NetRequest request = {.url = f->url, .authorization = f->auth, .range = range, .cancel = cancel,
                          .total_size = total, .stall_s = READ_STALL_S};
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

static void *openFile(const char *location, bool stream, int64_t *size)
{
    if (!davConfigured())
        return NULL;
    DavFile *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->stream = stream;
    snprintf(f->location, sizeof(f->location), "%s", location);
    char why[160];
    bool ok;
    if (davIsSmb()) {
        f->smb_share = true;
        f->smb = davSmbOpen(location, !stream, why, sizeof(why));
        ok = f->smb != NULL;
        *size = ok ? smbFileSize(f->smb) : -1;
    } else {
        ok = davStreamRequest(location, f->url, sizeof(f->url), f->auth, sizeof(f->auth)) &&
             (!stream || (f->net = netClientOpen()) != NULL);
        uint8_t first;
        long long total = -1;
        ok = ok && fetchRange(f, 0, &first, 1, &total, NULL) == 1 && total >= 0;  // Its length.
        *size = total;
    }
    if (!ok) {
        smbFileClose(f->smb);
        netClientClose(f->net);
        memset(f->auth, 0, sizeof(f->auth));
        free(f);
        return NULL;
    }
    return f;
}

static int64_t readFile(void *file, int64_t offset, void *to, size_t size, const volatile bool *cancel)
{
    DavFile *f = file;
    if (!f->smb_share) {
        long long total;
        return fetchRange(f, offset, to, size, &total, cancel);
    }
    char why[160];
    if (!f->smb)  // Its connection had gone: a new one.
        f->smb = davSmbOpen(f->location, !f->stream, why, sizeof(why));
    if (!f->smb)
        return -1;
    int64_t n = smbFileRead(f->smb, offset, to, size, cancel);
    if (n < 0 && f->stream && !(cancel && *cancel)) {
        smbFileClose(f->smb);  // The connection may be gone: a new one next time.
        f->smb = NULL;
    }
    return n;
}

static void closeFile(void *file)
{
    DavFile *f = file;
    if (!f)
        return;
    smbFileClose(f->smb);
    netClientClose(f->net);
    memset(f->auth, 0, sizeof(f->auth));
    free(f);
}

static const KFileRemote REMOTE = {openFile, readFile, closeFile, davList};

const KFileRemote *davFileRemote(void)
{
    return &REMOTE;
}
