// A Navidrome (Subsonic) server, read as the console reads it (tests/host_live/Makefile):
//   NAVIDROME=http://host:4533 NAVIDROME_USER=... NAVIDROME_PASS=... MUSIC=/music ./build/host_navidrome
// MUSIC is the server's songs on this computer ("NN - Title.ext" files), to compare with. Signs in (only
// the token goes out), reads the whole library page by page, reads songs' blocks with ranges as a peek and
// as a stream (the card's bytes), plays songs through kfile (the card's samples), gets a cover, tells the
// server a song is playing, and checks that a wrong password and nobody there each say so.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cover.h"
#include "decoder.h"
#include "kfile.h"
#include "net.h"
#include "subfile.h"
#include "subnet.h"
#include "util.h"

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
    SubAccount a = {.check_certificate = true};
    EXPECT(subNormalizeAddress(address, a.address, sizeof(a.address)), "address %s", address);
    snprintf(a.user, sizeof(a.user), "%s", user);
    EXPECT(subSetPassword(&a, pass, "0123abcd4567ef89"), "the token");
    subUseAccount(&a);
    subFileForget();
}

// The card's copy of a song: "<anything> - <title>.<ext>" somewhere under folder.
static bool findLocal(const char *folder, const char *title, const char *ext, char *out, size_t size)
{
    DIR *dir = opendir(folder);
    if (!dir)
        return false;
    char want[512];
    snprintf(want, sizeof(want), " - %s.%s", title, ext);
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", folder, e->d_name);
        size_t n = strlen(e->d_name), w = strlen(want);
        if (n > w && !strcmp(e->d_name + n - w, want)) {
            snprintf(out, size, "%s", path);
            found = true;
        } else {
            found = findLocal(path, title, ext, out, size);
        }
    }
    closedir(dir);
    return found;
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

// The whole song through kfile, as a stream and as a peek: the card's bytes.
static void readThrough(const char *location, const char *path)
{
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

// Played from the server, the song is the card's, sample for sample.
static void play(const char *location, const char *path)
{
    uint64_t a_frames, b_frames;
    double a_open, b_open;
    float *a = decodeAll(path, &a_frames, &a_open);
    float *b = decodeAll(location, &b_frames, &b_open);
    EXPECT(a && b && a_frames > 48000 && a_frames == b_frames && !memcmp(a, b, (size_t)a_frames * 2 * sizeof(float)),
           "%s: %llu frames, %llu from the server", location, (unsigned long long)a_frames,
           (unsigned long long)b_frames);
    printf("  played %s: %llu frames, the same; opened in %.0f ms\n", location, (unsigned long long)b_frames,
           b_open * 1000);
    free(a);
    free(b);
}

static void checkSongs(const Catalog *c)
{
    size_t played = 0, covers = 0;
    char seen[64] = "";
    for (size_t i = 0; i < c->song_count; i++) {
        const CatalogSong *s = &c->songs[i];
        const char *ext = strrchr(s->path, '.') + 1;
        printf("  %s | %s | %s | %s | %d.%d %d\n", s->path, s->title, s->artist, s->album_artist, s->disc, s->track,
               s->year);
        char path[1024];
        EXPECT(findLocal(g_music, s->title, ext, path, sizeof(path)), "the card's copy of %s", s->title);
        if (!strstr(seen, ext)) {  // One of each kind: it's slow with the sanitizers.
            strcat(seen, ext);
            readThrough(s->path, path);
            play(s->path, path);
            played++;
        }
        CoverImage large = {0}, small = {0};
        char cover[UTIL_PATH_MAX];
        bool has = subCoverLocation(s->path, 300, cover, sizeof(cover));
        bool got = coverLoadFromFolder(s->path, &large, 300, &small, 32);
        EXPECT(has == got, "%s: a cover %s, %s", s->title, has ? "named" : "not named", got ? "got" : "not got");
        covers += got;
        coverFree(&large);
        coverFree(&small);
    }
    printf("played %zu, covers %zu\n", played, covers);
    EXPECT(covers > 0, "no cover at all");
}

static bool neverStop(void)
{
    return false;
}

static void progress(size_t done, size_t total, void *user)
{
    (void)user;
    printf("  progress %zu / %zu\n", done, total);
}

int main(void)
{
    const char *address = getenv("NAVIDROME"), *user = getenv("NAVIDROME_USER"), *pass = getenv("NAVIDROME_PASS");
    g_music = getenv("MUSIC");
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!address || !user || !pass || !g_music || !netInit())
        return 2;
    use(address, user, pass);
    kfileSetRemote(KFILE_REMOTE_SERVER, subFileRemote());
    char text[256];
    EXPECT(subTest(text, sizeof(text)), "test: %s", text);
    printf("test: %s\n", text);

    Catalog c;
    catalogInit(&c);
    double start = secondsNow();
    EXPECT(subFetchLibrary(&c, neverStop, progress, NULL, text, sizeof(text)), "library: %s", text);
    EXPECT(catalogBuild(&c), "built");
    printf("library: %zu songs, %zu albums, %zu artists in %.2f s\n", c.song_count, c.album_count, c.artist_count,
           secondsNow() - start);
    EXPECT(c.song_count == 8, "8 songs, not %zu", c.song_count);
    checkSongs(&c);
    EXPECT(c.song_count && subScrobble(c.songs[0].path, false), "now playing");
    catalogFree(&c);
    EXPECT(!kfileOpen("sub:/-/NoSuchSong.flac", KFILE_STREAM), "a missing song opened");

    use(address, user, "wrong");
    EXPECT(!subTest(text, sizeof(text)) && strstr(text, "password"), "wrong password: %s", text);
    printf("wrong password: %s\n", text);
    use("http://127.0.0.1:1", user, pass);
    EXPECT(!subTest(text, sizeof(text)), "nobody there answered");
    printf("nobody there: %s\n", text);
    char other[512];
    snprintf(other, sizeof(other), "%s/nothing", address);  // Something answers, but not Subsonic.
    use(other, user, pass);
    EXPECT(!subTest(text, sizeof(text)), "a wrong path answered");
    printf("wrong path: %s\n", text);
    kfileSetRemote(KFILE_REMOTE_SERVER, NULL);
    subFileForget();
    netExit();
    printf(g_failed ? "%d FAILED\n" : "all good\n", g_failed);
    return g_failed ? 1 : 0;
}
