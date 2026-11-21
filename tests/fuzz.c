// A small mutation fuzzer for everything that parses files from the SD card:
// the audio decoders, the tag readers, the image decoder, and a music
// server's answers. Build it with
// AddressSanitizer and UndefinedBehaviorSanitizer (make -C tests fuzz).
//
// Usage: fuzz [ITERATIONS] [SEED]
// Each input is written to /tmp/kaiplayer_fuzz/ before it is parsed, so the
// last file there reproduces a crash.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cover.h"
#include "decoder.h"
#include "json.h"
#include "subsonic.h"
#include "tags.h"

#define WORK_DIR "/tmp/kaiplayer_fuzz"
#define HEADER_BYTES 4096
#define MAX_DECODE_FRAMES 400000

static uint64_t g_rng;

static uint64_t rnd(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545F4914F6CDD1Dull;
}

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

static void writeFile(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fwrite(data, 1, size, f);
    fclose(f);
}

// Applies 1 to 8 random edits, mostly in the headers. Returns the new size.
static size_t mutate(uint8_t *data, size_t size)
{
    static const uint8_t INTERESTING[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
    int edits = 1 + (int)(rnd() % 8);
    for (int i = 0; i < edits && size > 0; i++) {
        size_t limit = (rnd() % 4 != 0 && size > HEADER_BYTES) ? HEADER_BYTES : size;
        size_t pos = rnd() % limit;
        switch (rnd() % 5) {
        case 0: data[pos] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: data[pos] = INTERESTING[rnd() % sizeof(INTERESTING)]; break;
        case 2: data[pos] = (uint8_t)rnd(); break;
        case 3:
            if (pos + 4 <= size) {
                uint32_t v = rnd() % 2 ? 0xFFFFFFFFu : (uint32_t)rnd();
                memcpy(data + pos, &v, 4);
            }
            break;
        default:
            if (rnd() % 4 == 0)
                size = pos + 1;  // Truncate.
            break;
        }
    }
    return size;
}

static void fuzzAudioFile(const char *seed_path, const char *ext, int iterations)
{
    size_t seed_size = 0;
    uint8_t *seed = readFile(seed_path, &seed_size);
    if (!seed) {
        fprintf(stderr, "missing seed %s\n", seed_path);
        exit(2);
    }
    uint8_t *buf = malloc(seed_size);
    char path[128];
    snprintf(path, sizeof(path), WORK_DIR "/input.%s", ext);
    static float pcm[4096 * 2];

    for (int i = 0; i < iterations; i++) {
        memcpy(buf, seed, seed_size);
        writeFile(path, buf, mutate(buf, seed_size));

        TrackTags t;
        tagsInit(&t);
        tagsRead(path, &t);
        tagsFree(&t);

        Decoder *d = decoderOpen(path);
        if (!d)
            continue;
        uint64_t total = 0, got;
        while (total < MAX_DECODE_FRAMES && (got = decoderReadStereo(d, pcm, 4096)) > 0)
            total += got;
        decoderSeek(d, rnd() % 88200);
        decoderReadStereo(d, pcm, 4096);
        decoderClose(d);
    }
    printf("  %-28s %d inputs\n", seed_path, iterations);
    free(buf);
    free(seed);
}

static void fuzzImage(const char *seed_path, int iterations)
{
    size_t seed_size = 0;
    uint8_t *seed = readFile(seed_path, &seed_size);
    if (!seed) {
        fprintf(stderr, "missing seed %s\n", seed_path);
        exit(2);
    }
    uint8_t *buf = malloc(seed_size);
    for (int i = 0; i < iterations; i++) {
        memcpy(buf, seed, seed_size);
        size_t size = mutate(buf, seed_size);
        writeFile(WORK_DIR "/input.img", buf, size);
        CoverImage large, small;
        if (coverDecode(buf, size, &large, 128, &small, 16)) {
            coverFree(&large);
            coverFree(&small);
        }
    }
    printf("  %-28s %d inputs\n", seed_path, iterations);
    free(buf);
    free(seed);
}

// A server's answer (subsonic.h), into a catalog.
static void fuzzServerAnswer(const char *seed_path, int iterations)
{
    size_t seed_size = 0;
    uint8_t *seed = readFile(seed_path, &seed_size);
    if (!seed) {
        fprintf(stderr, "missing seed %s\n", seed_path);
        exit(2);
    }
    uint8_t *buf = malloc(seed_size);
    for (int i = 0; i < iterations; i++) {
        memcpy(buf, seed, seed_size);
        size_t size = mutate(buf, seed_size);
        writeFile(WORK_DIR "/input.json", buf, size);
        JsonDoc *doc = jsonParse((const char *)buf, size);
        const JsonValue *body;
        int code;
        char message[64];
        if (doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message))) {
            Catalog c;
            catalogInit(&c);
            size_t added = 0;
            if (subAddSongs(&c, jsonGet(body, "searchResult3"), "song", NULL, &added))
                catalogBuild(&c);
            catalogFree(&c);
        }
        jsonFree(doc);
    }
    printf("  %-28s %d inputs\n", seed_path, iterations);
    free(buf);
    free(seed);
}

int main(int argc, char **argv)
{
    int iterations = argc > 1 ? atoi(argv[1]) : 1000;
    g_rng = argc > 2 ? strtoull(argv[2], NULL, 10) : 0x5EED;
    mkdir(WORK_DIR, 0777);
    printf("fuzz: %d iterations per seed, seed %llu\n", iterations, (unsigned long long)g_rng);

    fuzzAudioFile("fixtures/tone.mp3", "mp3", iterations);
    fuzzAudioFile("fixtures/tone.flac", "flac", iterations);
    fuzzAudioFile("fixtures/tone.ogg", "ogg", iterations);
    fuzzAudioFile("fixtures/tone.wav", "wav", iterations);
    fuzzAudioFile("fixtures/tone.dsf", "dsf", iterations);
    fuzzAudioFile("fixtures/tone.dff", "dff", iterations);
    fuzzImage("fixtures/small_cover.jpg", iterations * 4);
    fuzzImage("fixtures/small_cover.png", iterations * 4);
    fuzzServerAnswer("fixtures/navidrome_search3.json", iterations * 4);
    printf("fuzz: done, no crashes\n");
    return 0;
}
