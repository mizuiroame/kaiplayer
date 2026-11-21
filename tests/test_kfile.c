// kfile: card files read as stdio reads them, and network files through a
// fake remote (the fixtures behind "dav:/" names), in blocks small enough
// that the fixtures cross many. The decoder, the tags and the covers read
// the same through it from the card and from the "network".
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "blockcache.h"
#include "browser.h"
#include "check.h"
#include "codecs_config.h"
#include "cover.h"
#include "decoder.h"
#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"
#include "kfile.h"
#include "library.h"
#include "tags.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define TEST_STREAM_BLOCK 4096
#define TEST_PEEK_BLOCK 1000  // Not a power of two: reads cross blocks unevenly.

// --- The fake remote: "dav:/x" is fixtures/x ---

typedef struct {
    FILE *f;
} FakeFile;

static const char *g_fake_root = FIXTURES;  // What "dav:/" is.
static int g_fake_open_files;
static int64_t g_fail_from = -1;  // Reads that reach this offset fail; -1: none.
static volatile bool g_hang;      // Reads wait until they're cancelled.

static void fakePath(const char *location, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", g_fake_root, location + strlen(DAV_PREFIX));
}

static void *fakeOpen(const char *location, bool stream, int64_t *size)
{
    (void)stream;
    char path[512];
    fakePath(location, path, sizeof(path));
    FakeFile *f = calloc(1, sizeof(*f));
    f->f = fopen(path, "rb");
    struct stat st;
    if (!f->f || stat(path, &st) != 0) {
        if (f->f)
            fclose(f->f);
        free(f);
        return NULL;
    }
    *size = st.st_size;
    __atomic_add_fetch(&g_fake_open_files, 1, __ATOMIC_RELAXED);
    return f;
}

static int64_t fakeRead(void *file, int64_t offset, void *to, size_t size, const volatile bool *cancel)
{
    FakeFile *f = file;
    while (g_hang) {
        if (cancel && *cancel)
            return -1;
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    if (g_fail_from >= 0 && offset + (int64_t)size > g_fail_from)
        return -1;
    if (fseek(f->f, (long)offset, SEEK_SET) != 0)
        return -1;
    return (int64_t)fread(to, 1, size, f->f);
}

static void fakeClose(void *file)
{
    FakeFile *f = file;
    fclose(f->f);
    free(f);
    __atomic_sub_fetch(&g_fake_open_files, 1, __ATOMIC_RELAXED);
}

static bool fakeList(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    char path[512];
    fakePath(location, path, sizeof(path));
    *out = NULL;
    *count = 0;
    DIR *dir = opendir(path);
    if (!dir) {
        snprintf(why, why_size, "no folder");
        return false;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        *out = realloc(*out, (*count + 1) * sizeof(DavEntry));
        DavEntry *d = &(*out)[(*count)++];
        memset(d, 0, sizeof(*d));
        snprintf(d->name, sizeof(d->name), "%s", e->d_name);
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        struct stat st;
        d->is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        d->size = d->is_dir ? -1 : st.st_size;
    }
    closedir(dir);
    return true;
}

static const KFileRemote FAKE = {fakeOpen, fakeRead, fakeClose, fakeList};

// The fake server: "sub:/<cover>/x" is fixtures/x, and any cover is preview/assets/cover.jpg.
static void *fakeServerOpen(const char *location, bool stream, int64_t *size)
{
    char path[512];
    const char *name = strrchr(location, '/') + 1;
    if (!strncmp(name, "cover-", 6))
        snprintf(path, sizeof(path), "%s", "dav:/../preview/assets/cover.jpg");
    else
        snprintf(path, sizeof(path), DAV_PREFIX "%s", name);
    return fakeOpen(path, stream, size);
}

static const KFileRemote FAKE_SERVER = {fakeServerOpen, fakeRead, fakeClose, NULL};

static uint8_t *readLocal(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n + 1);
    *size = fread(data, 1, (size_t)n, f);
    fclose(f);
    return data;
}

// --- The card ---

static void testLocal(void)
{
    const char *path = FIXTURES "/tone.flac";
    size_t size;
    uint8_t *want = readLocal(path, &size);
    KFile *f = kfileOpen(path, KFILE_STREAM);
    CHECK(f != NULL && want != NULL);
    if (!f || !want) {
        free(want);
        return;
    }
    CHECK(kfileStdio(f) != NULL);
    CHECK(kfileSize(f) == (int64_t)size);
    uint8_t buf[64];
    CHECK(kfileRead(f, buf, 4) == 4 && !memcmp(buf, "fLaC", 4));
    CHECK(kfileTell(f) == 4);  // kfileSize didn't move it.
    CHECK(kfileSeek(f, -10, SEEK_END) && kfileTell(f) == (int64_t)size - 10);
    CHECK(kfileRead(f, buf, sizeof(buf)) == 10 && !memcmp(buf, want + size - 10, 10));
    CHECK(kfileRead(f, buf, sizeof(buf)) == 0 && !kfileFailed(f));
    CHECK(kfileSeek(f, 100, SEEK_SET) && kfileSeek(f, -50, SEEK_CUR) && kfileTell(f) == 50);
    CHECK(kfileRead(f, buf, 8) == 8 && !memcmp(buf, want + 50, 8));
    CHECK(!kfileSeek(f, -1, SEEK_SET));
    kfileClose(f);
    kfileClose(NULL);
    CHECK(kfileOpen(FIXTURES "/missing.flac", KFILE_PEEK) == NULL);

    size_t got = 0;
    bool failed = true;
    uint8_t *all = kfileReadAll(path, 1 << 20, &got, &failed);
    CHECK(all && got == size && !memcmp(all, want, size) && !failed);
    free(all);
    CHECK(kfileReadAll(path, size - 1, &got, NULL) == NULL && got == 0);  // Larger than allowed.
    CHECK(kfileReadAll(FIXTURES "/missing.flac", 1 << 20, &got, NULL) == NULL);
    free(want);

    DavEntry *e;
    size_t n;
    char why[64];
    CHECK(!kfileList("dav:/", &e, &n, why, sizeof(why)));  // No remote yet.
    CHECK(kfileOpen("dav:/tone.flac", KFILE_PEEK) == NULL);
}

// --- Through the fake remote ---

// Reads a file at many places, in pieces of many sizes: the bytes are the card's.
static void checkBytes(const char *name, KFileMode mode)
{
    char path[256], location[256];
    snprintf(path, sizeof(path), FIXTURES "/%s", name);
    snprintf(location, sizeof(location), DAV_PREFIX "%s", name);
    size_t size;
    uint8_t *want = readLocal(path, &size);
    KFile *f = kfileOpen(location, mode);
    CHECK(f != NULL && kfileStdio(f) == NULL);
    if (!f || !want) {
        free(want);
        kfileClose(f);
        return;
    }
    CHECK(kfileSize(f) == (int64_t)size);
    uint8_t *buf = malloc(size + 4096);
    // All of it, in uneven pieces.
    size_t done = 0, n, piece = 1;
    while (done < size && (n = kfileRead(f, buf + done, piece)) > 0) {
        done += n;
        piece = piece * 3 % 4093 + 1;
    }
    CHECK(done == size && !memcmp(buf, want, size));
    // Here and there, forward and back.
    unsigned seed = 7;
    bool same = true;
    for (int i = 0; i < 200; i++) {
        seed = seed * 1103515245u + 12345u;
        int64_t at = (int64_t)(seed >> 8) % (int64_t)size;
        size_t length = (seed >> 3) % 3000 + 1;
        CHECK(kfileSeek(f, at, SEEK_SET));
        n = kfileRead(f, buf, length);
        size_t expected = (size_t)((int64_t)size - at) < length ? (size_t)((int64_t)size - at) : length;
        same = same && n == expected && !memcmp(buf, want + at, n) && kfileTell(f) == at + (int64_t)n;
    }
    CHECK(same);
    CHECK(kfileSeek(f, 0, SEEK_END) && kfileRead(f, buf, 10) == 0 && !kfileFailed(f));
    CHECK(kfileSeek(f, -3, SEEK_END) && kfileRead(f, buf, 10) == 3 && !memcmp(buf, want + size - 3, 3));
    CHECK(!kfileSeek(f, -1, SEEK_SET) && !kfileSeek(f, -(int64_t)size - 1, SEEK_END));
    kfileClose(f);
    free(buf);

    size_t got = 0;
    bool failed = true;
    uint8_t *all = kfileReadAll(location, 1 << 20, &got, &failed);
    CHECK(all && got == size && !memcmp(all, want, size) && !failed);
    free(all);
    CHECK(kfileReadAll(location, size - 1, &got, &failed) == NULL && !failed);  // Too large, not the network.
    free(want);
}

static void testRemoteBytes(void)
{
    checkBytes("tone.flac", KFILE_STREAM);
    checkBytes("tone.flac", KFILE_PEEK);
    checkBytes("tone.dsf", KFILE_STREAM);
    checkBytes("small_cover.jpg", KFILE_PEEK);
    CHECK(kfileOpen("dav:/missing.flac", KFILE_STREAM) == NULL);
    bool failed = false;
    size_t got;
    CHECK(kfileReadAll("dav:/missing.flac", 100, &got, &failed) == NULL && failed);
    DavEntry *e;
    size_t n;
    char why[64];
    CHECK(kfileList("dav:/", &e, &n, why, sizeof(why)) && n >= 8);
    free(e);
}

// A network that fails part way: the reads come short and say so.
static void testRemoteFailing(void)
{
    uint8_t buf[30000];
    for (int mode = 0; mode < 2; mode++) {
        g_fail_from = 10000;
        KFile *f = kfileOpen("dav:/tone.flac", mode ? KFILE_STREAM : KFILE_PEEK);
        CHECK(f != NULL);
        if (!f)
            continue;
        size_t n = kfileRead(f, buf, sizeof(buf));
        CHECK(n > 0 && n <= 10000 && kfileFailed(f));
        g_fail_from = -1;
        kfileClose(f);
    }
    g_fail_from = 0;  // Not even the first block: it doesn't open.
    CHECK(kfileOpen("dav:/tone.flac", KFILE_STREAM) == NULL);
    g_fail_from = -1;
}

static void *interruptLater(void *arg)
{
    (void)arg;
    nanosleep(&(struct timespec){0, 200000000}, NULL);
    kfileInterruptStreams();
    return NULL;
}

static double secondsNow(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

// A server that stops answering: an interrupt frees the reader at once.
static void testInterrupt(void)
{
    KFile *f = kfileOpen("dav:/tone.dsf", KFILE_STREAM);
    CHECK(f != NULL);
    if (!f)
        return;
    nanosleep(&(struct timespec){0, 50000000}, NULL);  // The window ahead fills.
    g_hang = true;
    pthread_t thread;
    pthread_create(&thread, NULL, interruptLater, NULL);
    double start = secondsNow();
    uint8_t buf[100];
    CHECK(kfileSeek(f, 120000, SEEK_SET));  // Far past what was fetched.
    CHECK(kfileRead(f, buf, sizeof(buf)) == 0 && kfileFailed(f));
    double waited = secondsNow() - start;
    CHECK(waited > 0.1 && waited < 3.0);
    pthread_join(thread, NULL);
    g_hang = false;
    kfileClose(f);
    // A file opened after the interrupt reads on.
    f = kfileOpen("dav:/tone.dsf", KFILE_STREAM);
    CHECK(f && kfileSeek(f, 120000, SEEK_SET) && kfileRead(f, buf, sizeof(buf)) == sizeof(buf));
    kfileClose(f);
}

// --- The decoder: through kfile it plays what the libraries read themselves ---

#define MAX_FRAMES 200000

// Interleaved stereo, as decoderReadStereo gives it.
static float *toStereo(float *native, uint64_t frames, unsigned channels)
{
    float *out = malloc((size_t)frames * 2 * sizeof(float) + 1);
    for (uint64_t i = 0; i < frames; i++) {
        out[i * 2] = native[i * channels];
        out[i * 2 + 1] = channels > 1 ? native[i * channels + 1] : native[i * channels];
    }
    free(native);
    return out;
}

// The file decoded by its library opening the file itself, as KaiPlayer did before kfile.
static float *decodeDirect(const char *path, uint64_t *frames)
{
    float *native = malloc(MAX_FRAMES * 8 * sizeof(float));
    unsigned channels = 0;
    *frames = 0;
    if (strstr(path, ".wav")) {
        drwav w;
        if (drwav_init_file(&w, path, NULL)) {
            *frames = drwav_read_pcm_frames_f32(&w, MAX_FRAMES, native);
            channels = w.channels;
            drwav_uninit(&w);
        }
    } else if (strstr(path, ".flac")) {
        drflac *fl = drflac_open_file(path, NULL);
        if (fl) {
            *frames = drflac_read_pcm_frames_f32(fl, MAX_FRAMES, native);
            channels = fl->channels;
            drflac_close(fl);
        }
    } else if (strstr(path, ".mp3")) {
        drmp3 m;
        if (drmp3_init_file(&m, path, NULL)) {
            *frames = drmp3_read_pcm_frames_f32(&m, MAX_FRAMES, native);
            channels = m.channels;
            drmp3_uninit(&m);
        }
    } else {
        int error;
        stb_vorbis *v = stb_vorbis_open_filename(path, &error, NULL);
        if (v) {
            channels = (unsigned)stb_vorbis_get_info(v).channels;
            int got, total = 0;
            while ((got = stb_vorbis_get_samples_float_interleaved(v, (int)channels, native + total * channels,
                                                                    (int)((MAX_FRAMES - total) * channels))) > 0)
                total += got;
            *frames = (uint64_t)total;
            stb_vorbis_close(v);
        }
    }
    return channels ? toStereo(native, *frames, channels) : (free(native), NULL);
}

static float *decodeWith(const char *path, uint64_t *frames)
{
    Decoder *d = decoderOpen(path);
    *frames = 0;
    if (!d)
        return NULL;
    float *out = malloc(MAX_FRAMES * 2 * sizeof(float));
    uint64_t got;
    while (*frames < MAX_FRAMES && (got = decoderReadStereo(d, out + *frames * 2, 1000)) > 0)
        *frames += got;
    decoderClose(d);
    return out;
}

static bool sameSamples(const float *a, const float *b, uint64_t frames)
{
    return a && b && !memcmp(a, b, (size_t)frames * 2 * sizeof(float));
}

static void testDecoderPaths(void)
{
    static const char *const NAMES[] = {"tone.wav", "tone.flac", "tone.mp3", "tone.ogg", "tone.dsf", "tone.dff"};
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        char path[256], location[256];
        snprintf(path, sizeof(path), FIXTURES "/%s", NAMES[i]);
        snprintf(location, sizeof(location), DAV_PREFIX "%s", NAMES[i]);
        uint64_t local_frames, remote_frames, direct_frames;
        float *local = decodeWith(path, &local_frames);
        float *remote = decodeWith(location, &remote_frames);
        CHECK(local && local_frames > 1000);
        CHECK(remote_frames == local_frames && sameSamples(local, remote, local_frames));  // The "network" plays the same.
        if (!strstr(NAMES[i], ".dsf") && !strstr(NAMES[i], ".dff")) {  // DSD has no library of its own to compare with.
            float *direct = decodeDirect(path, &direct_frames);
            CHECK(direct_frames == local_frames && sameSamples(local, direct, local_frames));
            free(direct);
        }
        // A seek lands on the same samples.
        Decoder *a = decoderOpen(path), *b = decoderOpen(location);
        CHECK(a && b);
        if (a && b) {
            float x[400 * 2], y[400 * 2];
            CHECK(decoderSeek(a, local_frames / 2) && decoderSeek(b, local_frames / 2));
            CHECK(decoderReadStereo(a, x, 400) == 400 && decoderReadStereo(b, y, 400) == 400);
            CHECK(!memcmp(x, y, sizeof(x)));
            CHECK(decoderTotalFrames(a) == decoderTotalFrames(b));
        }
        decoderClose(a);
        decoderClose(b);
        free(local);
        free(remote);
    }
}

// Tags and covers read over the "network" are the card's.
static void testTagsAndCovers(void)
{
    static const char *const NAMES[] = {"tone.mp3", "tone.flac", "tone.ogg", "tone.dsf", "tone.dff"};
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        char path[256], location[256];
        snprintf(path, sizeof(path), FIXTURES "/%s", NAMES[i]);
        snprintf(location, sizeof(location), DAV_PREFIX "%s", NAMES[i]);
        TrackTags a, b;
        tagsInit(&a);
        tagsInit(&b);
        CHECK(tagsRead(path, &a));
        CHECK(tagsRead(location, &b));
        CHECK(!strcmp(a.title, b.title) && !strcmp(a.artist, b.artist) && !strcmp(a.album, b.album));
        CHECK(a.picture_size == b.picture_size && (!a.picture_size || !memcmp(a.picture, b.picture, a.picture_size)));
        tagsFree(&a);
        tagsFree(&b);
    }
    TrackTags t;
    tagsInit(&t);
    CHECK(!tagsRead("dav:/missing.mp3", &t));
    g_fail_from = 500;
    CHECK(!tagsRead("dav:/tone.mp3", &t));  // The network failed: it says so.
    g_fail_from = -1;
    tagsFree(&t);
    CHECK(tagsRead("dav:/tone.wav", &t));  // A WAV has none to read.

    CoverImage large_a, small_a, large_b, small_b;
    g_fake_root = "preview/assets";  // A folder with a cover.jpg.
    CHECK(coverLoadFromFolder("preview/assets/track.flac", &large_a, 64, &small_a, 16));
    CHECK(coverLoadFromFolder("dav:/track.flac", &large_b, 64, &small_b, 16));
    g_fake_root = FIXTURES;
    CHECK(large_a.size == large_b.size && large_a.average == large_b.average &&
          !memcmp(large_a.pixels, large_b.pixels, (size_t)large_a.size * large_a.size * 4));
    coverFree(&large_a);
    coverFree(&small_a);
    coverFree(&large_b);
    coverFree(&small_b);
}

// The network folder as a library and in Folders: listed through the remote.
static void testNetworkLibrary(void)
{
    LibraryFiles files;
    char why[128];
    CHECK(libraryScanNetwork(&files, "dav:/", NULL, why, sizeof(why)));
    CHECK(files.count == 6);  // The tones; not the pictures, the scripts or __pycache__'s.
    bool sorted = true, sized = true;
    for (size_t i = 0; i < files.count; i++) {
        sorted = sorted && (i == 0 || utilCompareNames(files.files[i - 1].path, files.files[i].path) < 0);
        sized = sized && files.files[i].size > 0 && !strncmp(files.files[i].path, "dav:/tone.", 10);
    }
    CHECK(sorted && sized);
    libraryFilesFree(&files);
    CHECK(!libraryScanNetwork(&files, "dav:/none", NULL, why, sizeof(why)) && !strcmp(why, "no folder"));
    CHECK(files.count == 0 && files.files == NULL);

    TrackList list;
    CHECK(libraryScan(&list, "dav:/", true) && list.count == 6 && !strcmp(list.paths[0], "dav:/tone.dff"));
    trackListFree(&list);

    Browser b = {0};
    CHECK(browserOpen(&b, "dav:/") && !strcmp(b.path, "dav:/") && !b.problem[0]);
    size_t dirs = 0, songs = 0;
    for (size_t i = 0; i < b.count; i++) {
        dirs += b.entries[i].is_dir;
        songs += !b.entries[i].is_dir;
    }
    CHECK(songs == 6 && dirs <= 1);  // A __pycache__ folder may be there.
    CHECK(!browserOpen(&b, "dav:/none") && !strcmp(b.problem, "no folder") && !strcmp(b.path, "dav:/"));
    browserFree(&b);
}

// A server's songs and covers go to its own remote, not the network folder's.
static void testServerLocations(void)
{
    CHECK(kfileIsRemote("sub:/-/tone.flac") && kfileIsRemote("dav:/a.flac") && !kfileIsRemote("sdmc:/a.flac"));
    CHECK(kfileOpen("sub:/-/tone.flac", KFILE_PEEK) == NULL);  // No server set up.
    kfileSetRemote(KFILE_REMOTE_SERVER, &FAKE_SERVER);
    size_t local_size, remote_size;
    uint8_t *local = readLocal(FIXTURES "/tone.flac", &local_size);
    uint8_t *remote = kfileReadAll("sub:/al-1/tone.flac", 1u << 20, &remote_size, NULL);
    CHECK(remote && remote_size == local_size && !memcmp(local, remote, local_size));
    free(local);
    free(remote);
    TrackTags t;
    tagsInit(&t);
    CHECK(tagsRead("sub:/al-1/tone.mp3", &t) && !strcmp(t.title, "Test Title"));
    tagsFree(&t);
    CoverImage large, small;  // The server's cover, not a folder's listing.
    CHECK(coverLoadFromFolder("sub:/al-1/tone.flac", &large, 64, &small, 16));
    coverFree(&large);
    coverFree(&small);
    CHECK(!coverLoadFromFolder("sub:/-/tone.flac", &large, 64, &small, 16));  // It has none.
    DavEntry *entries;
    size_t count;
    char why[64];
    CHECK(!kfileList("sub:/", &entries, &count, why, sizeof(why)));  // No list: no folders.
    kfileSetRemote(KFILE_REMOTE_SERVER, NULL);
}

static void testBlockCache(void)
{
    BlockSlot slots[4] = {{0}};
    int64_t reader = 0;
    CHECK(blockNextWanted(slots, 4, &reader, 1, -1, 3) == 0);  // Only the first until the length is known.
    slots[0] = (BlockSlot){.index = 0, .state = BLOCK_READY, .size = 10};
    CHECK(blockNextWanted(slots, 4, &reader, 1, -1, 3) == -1);
    CHECK(blockNextWanted(slots, 4, &reader, 1, 10, 3) == 1);
    slots[1] = (BlockSlot){.index = 1, .state = BLOCK_READY, .size = 10};
    slots[2] = (BlockSlot){.index = 2, .state = BLOCK_LOADING};
    CHECK(blockNextWanted(slots, 4, &reader, 1, 10, 3) == -1);  // The window of 3 is full.
    CHECK(blockNextWanted(slots, 4, &reader, 1, 2, 3) == -1);   // The file has 2 blocks.
    CHECK(blockVictim(slots, 4, &reader, 1, 3) == 3);           // An empty slot first.
    slots[3] = (BlockSlot){.index = 7, .state = BLOCK_READY, .size = 10};
    CHECK(blockVictim(slots, 4, &reader, 1, 3) == 3);           // Then one past the window.
    reader = 5;
    CHECK(blockVictim(slots, 4, &reader, 1, 3) == 0);           // Then the furthest behind.
    CHECK(blockIsWanted(7, &reader, 1, 3) && !blockIsWanted(8, &reader, 1, 3) && !blockIsWanted(4, &reader, 1, 3));
}

void testKfile(void)
{
    testBlockCache();
    testLocal();
    kfileSetBlockSizes(TEST_STREAM_BLOCK, TEST_PEEK_BLOCK);
    kfileSetRemote(KFILE_REMOTE_FOLDER, &FAKE);
    testRemoteBytes();
    testRemoteFailing();
    testInterrupt();
    testDecoderPaths();
    testTagsAndCovers();
    testNetworkLibrary();
    testServerLocations();
    CHECK(g_fake_open_files == 0);  // Every file opened was closed.
    kfileSetRemote(KFILE_REMOTE_FOLDER, NULL);
    kfileSetBlockSizes(0, 0);
}
