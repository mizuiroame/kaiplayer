// The network folder over SMB against a real share, read as the console
// reads it (tests/host_live/Makefile):
//   SMB_ADDRESS='\\host:4451\Music' SMB_USER=... SMB_PASS=... MUSIC=/music ./build/host_smb
// MUSIC is the same songs on this computer, to compare with. An http://
// address tests a WebDAV folder the same way (but smbnet's own reads). Tests the
// account, lists folders, reads a song's blocks on its own connection and on
// the shared one (and after that one is dropped), plays songs through kfile
// as the player does (the same samples as the card's), reads their tags and
// covers, scans the library, and checks that a wrong password, a wrong share
// and nobody there each say so.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cover.h"
#include "davfile.h"
#include "davnet.h"
#include "decoder.h"
#include "kfile.h"
#include "library.h"
#include "net.h"
#include "smbnet.h"
#include "tags.h"

#define MAX_FRAMES (60 * 48000)

static int g_failed;
static const char *g_music;

#define EXPECT(cond, ...)                 \
    do {                                  \
        if (!(cond)) {                    \
            g_failed++;                   \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                 \
        }                                 \
    } while (0)

static double secondsNow(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

static void use(const char *address, const char *user, const char *pass)
{
    DavAccount a = {0};
    EXPECT(davNormalizeAddress(address, a.address, sizeof(a.address)), "address %s", address);
    snprintf(a.user, sizeof(a.user), "%s", user);
    snprintf(a.password, sizeof(a.password), "%s", pass);
    davUseAccount(&a);
    smbForget();
}

// The card's copy of a "dav:/" song.
static void localPath(const char *location, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", g_music, location + strlen(DAV_PREFIX));
}

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

// A song's first and last bytes with smbnet itself, on its own connection and on the shared one.
static void readBlocks(const char *location)
{
    for (int shared = 0; shared < 2; shared++) {
        char why[256];
        SmbFile *f = davSmbOpen(location, shared, why, sizeof(why));
        EXPECT(f, "open %s: %s", location, why);
        if (!f)
            continue;
        int64_t size = smbFileSize(f);
        static uint8_t block[1 << 20];
        int64_t n = smbFileRead(f, 0, block, sizeof(block), NULL);
        EXPECT(n == (int64_t)sizeof(block) || n == size, "first block: %lld of %lld", (long long)n, (long long)size);
        printf("  %s%s: %lld bytes, starts %02x%02x%02x%02x\n", shared ? "(shared) " : "", location, (long long)size,
               block[0], block[1], block[2], block[3]);
        if (shared)
            smbForget();  // The shared connection goes: the next read makes it again, and opens the file again.
        n = smbFileRead(f, size - 1000, block, sizeof(block), NULL);
        EXPECT(n == 1000, "last block%s: %lld", shared ? " after the connection went" : "", (long long)n);
        EXPECT(smbFileRead(f, size, block, 10, NULL) == 0, "past the end");
        smbFileClose(f);
    }
}

// The whole song through kfile, as a stream and as a peek: the card's bytes.
static void readThrough(const char *location)
{
    char path[1024];
    localPath(location, path, sizeof(path));
    size_t size;
    uint8_t *want = readLocal(path, &size);
    EXPECT(want, "the card's copy of %s", location);
    if (!want)
        return;
    uint8_t *got = malloc(size + 1);
    for (int mode = 0; mode < 2; mode++) {
        double start = secondsNow();
        KFile *f = kfileOpen(location, mode ? KFILE_STREAM : KFILE_PEEK);
        EXPECT(f, "kfile %s", location);
        if (!f)
            continue;
        EXPECT(kfileSize(f) == (int64_t)size, "size %lld, not %zu", (long long)kfileSize(f), size);
        size_t done = 0, n;
        while (done < size && (n = kfileRead(f, got + done, 100000)) > 0)
            done += n;
        EXPECT(done == size && !memcmp(got, want, size) && !kfileFailed(f), "%s bytes differ", mode ? "stream" : "peek");
        // A seek back and forth, as a decoder's.
        EXPECT(kfileSeek(f, (int64_t)size / 3, SEEK_SET) && kfileRead(f, got, 5000) == 5000 &&
                   !memcmp(got, want + size / 3, 5000), "seek");
        kfileClose(f);
        printf("  %s %s: %.1f MB/s\n", mode ? "stream" : "peek", location, size / 1e6 / (secondsNow() - start));
    }
    free(got);
    free(want);
}

static float *decodeAll(const char *path, uint64_t *frames, double *open_s)
{
    double start = secondsNow();
    Decoder *d = decoderOpen(path);
    *open_s = secondsNow() - start;
    *frames = 0;
    if (!d)
        return NULL;
    float *out = malloc((size_t)MAX_FRAMES * 2 * sizeof(float));
    uint64_t got;
    while (*frames < MAX_FRAMES - 4096 && (got = decoderReadStereo(d, out + *frames * 2, 4096)) > 0)
        *frames += got;
    decoderClose(d);
    return out;
}

// The song played from the share is the card's, sample for sample; so are its tags and cover.
static void play(const char *location)
{
    char path[1024];
    localPath(location, path, sizeof(path));
    uint64_t a_frames, b_frames;
    double a_open, b_open;
    float *a = decodeAll(path, &a_frames, &a_open);
    float *b = decodeAll(location, &b_frames, &b_open);
    EXPECT(a && b && a_frames > 48000 && a_frames == b_frames && !memcmp(a, b, (size_t)a_frames * 2 * sizeof(float)),
           "%s: %llu frames, %llu from the share", location, (unsigned long long)a_frames, (unsigned long long)b_frames);
    printf("  played %s: %llu frames, the same; opened in %.0f ms\n", location, (unsigned long long)b_frames,
           b_open * 1000);
    free(a);
    free(b);
    TrackTags x, y;
    tagsInit(&x);
    tagsInit(&y);
    tagsRead(path, &x);
    EXPECT(tagsRead(location, &y), "tags of %s", location);
    EXPECT(!strcmp(x.title, y.title) && !strcmp(x.artist, y.artist) && !strcmp(x.album, y.album) &&
               x.track_number == y.track_number && x.picture_size == y.picture_size &&
               x.has_track_gain == y.has_track_gain && x.track_gain_db == y.track_gain_db && y.title[0],
           "tags of %s differ", location);
    printf("  tags: %s / %s / %s, picture %zu bytes, gain %s%.2f dB\n", y.title, y.artist, y.album, y.picture_size,
           y.has_track_gain ? "" : "none ", y.track_gain_db);
    tagsFree(&x);
    tagsFree(&y);
    CoverImage large = {0}, small = {0}, card_large = {0}, card_small = {0};
    bool card = coverLoadFromFolder(path, &card_large, 64, &card_small, 16);
    bool share = coverLoadFromFolder(location, &large, 64, &small, 16);
    EXPECT(card == share && (!card || card_large.average == large.average), "the folder's cover");
    printf("  cover: %s\n", share ? "found" : "none");
    coverFree(&large);
    coverFree(&small);
    coverFree(&card_large);
    coverFree(&card_small);
}

static void scan(void)
{
    double start = secondsNow();
    LibraryFiles files;
    char why[160] = "";
    EXPECT(libraryScanNetwork(&files, DAV_ROOT, NULL, why, sizeof(why)), "scan: %s", why);
    printf("scan: %zu songs in %.2f s\n", files.count, secondsNow() - start);
    for (size_t i = 0; i < files.count; i++)
        printf("  %s %lld %lld\n", files.files[i].path, (long long)files.files[i].size,
               (long long)files.files[i].modified);
    EXPECT(files.count == 12, "12 songs, not %zu", files.count);
    libraryFilesFree(&files);
}

int main(void)
{
    const char *address = getenv("SMB_ADDRESS"), *user = getenv("SMB_USER"), *pass = getenv("SMB_PASS");
    g_music = getenv("MUSIC");
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!address || !user || !pass || !g_music || !netInit())
        return 2;
    use(address, user, pass);
    kfileSetRemote(KFILE_REMOTE_FOLDER, davFileRemote());
    char text[256];
    EXPECT(davTest(text, sizeof(text)), "test: %s", text);
    printf("test: %s\n", text);
    DavEntry *e;
    size_t n;
    EXPECT(davList("dav:/Aurora Fields/Paper Boats", &e, &n, text, sizeof(text)) && n == 4, "an album: %s", text);
    for (size_t i = 0; i < n; i++)
        printf("  %s%s %lld %lld\n", e[i].name, e[i].is_dir ? "/" : "", (long long)e[i].size, (long long)e[i].modified);
    free(e);

    bool smb = davIsSmb();  // WebDAV (an http:// address) goes through the same, without smbnet's own reads.
    if (smb)
        readBlocks("dav:/Aurora Fields/Paper Boats/01 - Morning Light.flac");
    readThrough("dav:/Aurora Fields/Paper Boats/02 - Paper Boats.flac");
    play("dav:/Aurora Fields/Paper Boats/01 - Morning Light.flac");
    play("dav:/Aurora Fields/Neon Harbor/01 - Neon Harbor.mp3");
    play("dav:/The Night Drive Band/Night Drive/02 - Tail Lights.ogg");
    play("dav:/Aurora Fields/Tidal Lines/01 - Salt Air.m4a");          // AAC, its cover inside.
    play("dav:/Aurora Fields/Tidal Lines/02 - Harbour Lights.m4a");    // ALAC, 24-bit, 96 kHz.
    play("dav:/The Night Drive Band/Night Drive/04 - Dawn Patrol.opus");
    play("dav:/The Night Drive Band/Night Drive/05 - Rear View.ogg");  // Opus in an .ogg file.
    scan();
    EXPECT(!davList("dav:/Not a folder", &e, &n, text, sizeof(text)), "a missing folder listed");
    printf("missing folder: %s\n", text);
    EXPECT(!kfileOpen("dav:/Not a song.flac", KFILE_STREAM), "a missing song opened");

    use(address, user, "wrong");
    EXPECT(!davTest(text, sizeof(text)) && strstr(text, "password"), "wrong password: %s", text);
    printf("wrong password: %s\n", text);
    char other[512];
    snprintf(other, sizeof(other), "%s%s", address, smb ? "" : "/");
    char *last = strrchr(other, '\\') ? strrchr(other, '\\') : strrchr(other, '/');
    if (last)
        snprintf(last + 1, sizeof(other) - (size_t)(last + 1 - other), "NoSuchShare");
    use(other, user, pass);
    EXPECT(!davTest(text, sizeof(text)), "a missing share answered");
    printf("wrong share: %s\n", text);
    use(smb ? "\\\\127.0.0.1:1\\Music" : "http://127.0.0.1:1/", user, pass);
    EXPECT(!davTest(text, sizeof(text)), "nobody there answered");
    printf("nobody there: %s\n", text);
    kfileSetRemote(KFILE_REMOTE_FOLDER, NULL);
    smbForget();
    davFileForget();
    netExit();
    printf(g_failed ? "%d FAILED\n" : "all good\n", g_failed);
    return g_failed ? 1 : 0;
}
