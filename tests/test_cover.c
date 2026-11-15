// Cover decoding tests, and end-to-end tests on real MP3, FLAC and Ogg files
// made with ffmpeg (tests/fixtures, see fixtures/README.md).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "check.h"
#include "cover.h"
#include "decoder.h"
#include "tags.h"

#define LARGE 400
#define SMALL 56

static uint8_t *readFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)len);
    *size = data ? fread(data, 1, (size_t)len, f) : 0;
    fclose(f);
    return data;
}

static bool coverLooksValid(const CoverImage *c, int size)
{
    return c->size == size && c->pixels && (c->average & 0xFFFFFF) != 0;
}

static void copyFile(const char *from, const char *to)
{
    size_t size = 0;
    uint8_t *data = readFile(from, &size);
    FILE *f = fopen(to, "wb");
    if (f && data)
        fwrite(data, 1, size, f);
    if (f)
        fclose(f);
    free(data);
}

// The host filesystem is case-sensitive, like the Switch 2 SD card.
static void testFolderCoverNames(void)
{
    CHECK(coverNamePriority("cover.jpg") == 0);
    CHECK(coverNamePriority("Cover.JPG") == 0);
    CHECK(coverNamePriority("FOLDER.jpeg") == 1);
    CHECK(coverNamePriority("AlbumArt_{1234}_Large.jpg") == 3);
    CHECK(coverNamePriority("scan 01.png") == 4);
    CHECK(coverNamePriority("covers.jpg") == 4);  // Not exactly "cover".
    CHECK(coverNamePriority("cover.txt") == -1);

    CoverImage large, small;
    mkdir(TMP_ROOT "/art", 0777);
    mkdir(TMP_ROOT "/art/a", 0777);
    copyFile(FIXTURES "/small_cover.jpg", TMP_ROOT "/art/a/Cover.JPG");
    copyFile(FIXTURES "/small_cover.png", TMP_ROOT "/art/a/back.png");
    CHECK(coverLoadFromFolder(TMP_ROOT "/art/a/song.flac", &large, LARGE, &small, SMALL));
    coverFree(&large);
    coverFree(&small);

    mkdir(TMP_ROOT "/art/b", 0777);  // A single image with any name is used.
    copyFile(FIXTURES "/small_cover.png", TMP_ROOT "/art/b/Booklet 01.png");
    CHECK(coverLoadFromFolder(TMP_ROOT "/art/b/song.flac", &large, LARGE, &small, SMALL));
    coverFree(&large);
    coverFree(&small);

    mkdir(TMP_ROOT "/art/c", 0777);  // Several unnamed images: none is guessed.
    copyFile(FIXTURES "/small_cover.png", TMP_ROOT "/art/c/scan1.png");
    copyFile(FIXTURES "/small_cover.png", TMP_ROOT "/art/c/scan2.png");
    CHECK(!coverLoadFromFolder(TMP_ROOT "/art/c/song.flac", &large, LARGE, &small, SMALL));
}

void testCover(void)
{
    testFolderCoverNames();

    CoverImage large, small;
    CHECK(coverLoadFromFolder("preview/assets/track.flac", &large, LARGE, &small, SMALL));
    CHECK(coverLooksValid(&large, LARGE) && coverLooksValid(&small, SMALL));
    coverFree(&large);
    coverFree(&small);
    CHECK(!coverLoadFromFolder(FIXTURES "/none/track.flac", &large, LARGE, &small, SMALL));

    size_t size = 0;
    uint8_t *jpg = readFile(FIXTURES "/small_cover.jpg", &size);
    CHECK(jpg && coverDecode(jpg, size, &large, LARGE, &small, SMALL));
    CHECK(coverLooksValid(&large, LARGE) && coverLooksValid(&small, SMALL));
    coverFree(&large);
    coverFree(&small);
    CHECK(!coverDecode(jpg, size / 2, &large, LARGE, &small, SMALL) || large.pixels);  // Truncated: no crash.
    coverFree(&large);
    coverFree(&small);
    free(jpg);

    // A PNG header that announces 5000 x 5000 is refused before decoding.
    static const uint8_t huge_png[] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
        0, 0, 0x13, 0x88, 0, 0, 0x13, 0x88, 8, 6, 0, 0, 0, 0, 0, 0, 0,
    };
    CHECK(!coverDecode(huge_png, sizeof(huge_png), &large, LARGE, &small, SMALL));
    CHECK(!large.pixels && !small.pixels);
    CHECK(!coverDecode((const uint8_t *)"not an image", 12, &large, LARGE, &small, SMALL));
    CHECK(!coverDecode(NULL, 0, &large, LARGE, &small, SMALL));
}

static void checkFixture(const char *name, uint64_t min_frames, uint64_t max_frames)
{
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "/%s", name);

    Decoder *d = decoderOpen(path);
    CHECK(d != NULL);
    if (d) {
        CHECK(decoderSampleRate(d) == 44100);
        static float buf[1024 * 2];
        uint64_t total = 0, got;
        float peak = 0.0f;
        while ((got = decoderReadStereo(d, buf, 1024)) > 0) {
            for (uint64_t i = 0; i < got * 2; i++)
                peak = fmaxf(peak, fabsf(buf[i]));
            total += got;
        }
        CHECK(total >= min_frames && total <= max_frames);
        CHECK(peak > 0.05f);  // The 440 Hz tone is there.
        CHECK(decoderSeek(d, 22050));
        decoderClose(d);
    }

    TrackTags t;
    tagsInit(&t);
    tagsRead(path, &t);
    CHECK(strcmp(t.title, "Test Title") == 0);
    CHECK(strcmp(t.artist, "T\xC3\xABst \xC3\x81rtist") == 0);
    CHECK(strcmp(t.album, "Fixture Album") == 0);
    CoverImage large, small;
    CHECK(t.picture && coverDecode(t.picture, t.picture_size, &large, LARGE, &small, SMALL));
    coverFree(&large);
    coverFree(&small);
    tagsFree(&t);
}

void testFixtures(void)
{
    // MP3 adds encoder delay and padding, so its length is approximate.
    checkFixture("tone.mp3", 44100, 47000);
    checkFixture("tone.flac", 44100, 44100);
    checkFixture("tone.ogg", 44100, 44100);
}
