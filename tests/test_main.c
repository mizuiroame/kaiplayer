// Host tests for the modules that do not touch Switch services.
// Build and run with: make -C tests
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "browser.h"
#include "codecs_config.h"
#include "decoder.h"
#include "dr_wav.h"
#include "library.h"
#include "queue.h"
#include "resampler.h"
#include "util.h"

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define TMP_ROOT "/tmp/kaiplayer_test"

static void testUtil(void)
{
    CHECK(utilIsAudioFile("song.MP3"));
    CHECK(utilIsAudioFile("a.flac") && utilIsAudioFile("a.ogg") && utilIsAudioFile("a.wav"));
    CHECK(!utilIsAudioFile("cover.jpg"));
    CHECK(!utilIsAudioFile(".mp3"));
    CHECK(!utilIsAudioFile("noext"));

    CHECK(strcmp(utilFileName("sdmc:/a/b.mp3"), "b.mp3") == 0);
    CHECK(strcmp(utilFileName("plain"), "plain") == 0);

    char out[64];
    CHECK(utilJoinPath(out, sizeof(out), "sdmc:/", "Music") && strcmp(out, "sdmc:/Music") == 0);
    CHECK(utilJoinPath(out, sizeof(out), "sdmc:/Music", "x.mp3") && strcmp(out, "sdmc:/Music/x.mp3") == 0);
    CHECK(!utilJoinPath(out, 8, "sdmc:/Music", "x.mp3"));

    char path[64] = "sdmc:/Music/Rock";
    CHECK(utilParentPath(path) && strcmp(path, "sdmc:/Music") == 0);
    CHECK(utilParentPath(path) && strcmp(path, "sdmc:/") == 0);
    CHECK(!utilParentPath(path) && strcmp(path, "sdmc:/") == 0);
    strcpy(path, "sdmc:/Music/");
    CHECK(utilParentPath(path) && strcmp(path, "sdmc:/") == 0);

    CHECK(utilCompareNames("abc", "ABC") == 0);
    CHECK(utilCompareNames("a", "B") < 0);
    CHECK(utilCompareNames("b", "a") > 0);

    utilFormatTime(out, sizeof(out), 65.9);
    CHECK(strcmp(out, "1:05") == 0);
    utilFormatTime(out, sizeof(out), 3725);
    CHECK(strcmp(out, "1:02:05") == 0);
    utilFormatTime(out, sizeof(out), -4);
    CHECK(strcmp(out, "0:00") == 0);
}

static void testQueueSequential(void)
{
    Queue q;
    CHECK(!queueInit(&q, 0, 0, false, 1));
    CHECK(queueInit(&q, 3, 1, false, 1));
    CHECK(queueCurrent(&q) == 1);

    q.repeat = REPEAT_OFF;
    CHECK(queueNext(&q, false) && queueCurrent(&q) == 2);
    CHECK(!queueNext(&q, false));             // End of list.
    CHECK(queueNext(&q, true) && queueCurrent(&q) == 0);  // A skip wraps.

    queuePrev(&q);
    CHECK(queueCurrent(&q) == 2);

    q.repeat = REPEAT_ALL;
    CHECK(queueNext(&q, false) && queueCurrent(&q) == 0);

    q.repeat = REPEAT_ONE;
    CHECK(queueNext(&q, false) && queueCurrent(&q) == 0);
    CHECK(queueNext(&q, true) && queueCurrent(&q) == 1);

    queueJumpTo(&q, 2);
    CHECK(queueCurrent(&q) == 2);
    queueFree(&q);
}

static void testQueueShuffle(void)
{
    enum { N = 50 };
    Queue q;
    CHECK(queueInit(&q, N, 17, true, 12345));
    CHECK(queueCurrent(&q) == 17);

    int seen[N] = {0};
    bool identity = true;
    for (size_t i = 0; i < N; i++) {
        seen[q.order[i]]++;
        identity &= q.order[i] == i;
    }
    for (int i = 0; i < N; i++)
        CHECK(seen[i] == 1);
    CHECK(!identity);

    CHECK(queueNext(&q, true));
    size_t current = queueCurrent(&q);
    queueSetShuffle(&q, false);
    CHECK(queueCurrent(&q) == current);
    queueSetShuffle(&q, true);
    CHECK(queueCurrent(&q) == current);
    queueFree(&q);
}

typedef struct {
    float value;
    uint64_t remaining;
    uint64_t index;
} RampSource;

static uint64_t rampRead(void *ctx, float *out, uint64_t frames)
{
    RampSource *s = ctx;
    uint64_t n = frames < s->remaining ? frames : s->remaining;
    for (uint64_t i = 0; i < n; i++) {
        float v = s->value < 0 ? (float)(s->index + i) : s->value;
        out[i * 2] = v;
        out[i * 2 + 1] = -v;
    }
    s->remaining -= n;
    s->index += n;
    return n;
}

static void testResampler(void)
{
    static Resampler r;
    static float out[20000 * 2];

    // Same rate: the samples come through unchanged.
    RampSource ramp = {-1.0f, 5000, 0};
    resamplerReset(&r, 48000, 48000);
    uint64_t n = resamplerRun(&r, rampRead, &ramp, out, 20000);
    CHECK(n >= 4998 && n <= 5000);
    bool exact = true;
    for (uint64_t i = 0; i < n; i++)
        exact &= out[i * 2] == (float)i && out[i * 2 + 1] == -(float)i;
    CHECK(exact);

    // 44.1 kHz to 48 kHz: about 48/44.1 times as many frames, values kept.
    RampSource flat = {0.5f, 4410, 0};
    resamplerReset(&r, 44100, 48000);
    n = resamplerRun(&r, rampRead, &flat, out, 20000);
    CHECK(n >= 4795 && n <= 4800);
    bool constant = true;
    for (uint64_t i = 0; i < n; i++)
        constant &= fabsf(out[i * 2] - 0.5f) < 1e-6f && fabsf(out[i * 2 + 1] + 0.5f) < 1e-6f;
    CHECK(constant);

    // Output in small pieces gives the same total.
    RampSource pieces = {0.25f, 4410, 0};
    resamplerReset(&r, 44100, 48000);
    uint64_t total = 0, got;
    while ((got = resamplerRun(&r, rampRead, &pieces, out, 333)) > 0)
        total += got;
    CHECK(total == n);
}

static bool writeWav(const char *path, uint32_t channels, uint32_t rate, uint64_t frames)
{
    drwav_data_format format = {
        .container = drwav_container_riff,
        .format = DR_WAVE_FORMAT_PCM,
        .channels = channels,
        .sampleRate = rate,
        .bitsPerSample = 16,
    };
    drwav wav;
    if (!drwav_init_file_write(&wav, path, &format, NULL))
        return false;
    int16_t *samples = malloc(frames * channels * sizeof(int16_t));
    for (uint64_t i = 0; i < frames; i++) {
        for (uint32_t c = 0; c < channels; c++)
            samples[i * channels + c] = (int16_t)(c == 0 ? 8192 : -8192);
    }
    uint64_t written = drwav_write_pcm_frames(&wav, frames, samples);
    free(samples);
    drwav_uninit(&wav);
    return written == frames;
}

static void testDecoder(void)
{
    const char *mono = TMP_ROOT "/mono.wav";
    CHECK(writeWav(mono, 1, 44100, 44100));
    Decoder *d = decoderOpen(mono);
    CHECK(d != NULL);
    if (d) {
        CHECK(decoderSampleRate(d) == 44100);
        CHECK(decoderTotalFrames(d) == 44100);
        float buf[64 * 2];
        CHECK(decoderReadStereo(d, buf, 64) == 64);
        CHECK(fabsf(buf[0] - 0.25f) < 1e-3f && buf[0] == buf[1]);  // Mono is duplicated.
        CHECK(decoderSeek(d, 44000));
        uint64_t rest = 0, got;
        while ((got = decoderReadStereo(d, buf, 64)) > 0)
            rest += got;
        CHECK(rest == 100);
        decoderClose(d);
    }

    const char *quad = TMP_ROOT "/quad.wav";
    CHECK(writeWav(quad, 4, 48000, 100));
    d = decoderOpen(quad);
    CHECK(d != NULL);
    if (d) {
        float buf[4 * 2];
        CHECK(decoderReadStereo(d, buf, 4) == 4);
        CHECK(buf[0] > 0.2f && buf[1] < -0.2f);  // Front left and right kept.
        decoderClose(d);
    }

    // Sample rates from a crafted header outside the playable range are refused.
    const char *fast = TMP_ROOT "/fast.wav";
    CHECK(writeWav(fast, 2, 2000000, 16));
    CHECK(decoderOpen(fast) == NULL);
    const char *slow = TMP_ROOT "/slow.wav";
    CHECK(writeWav(slow, 2, 500, 16));
    CHECK(decoderOpen(slow) == NULL);

    CHECK(decoderOpen(TMP_ROOT "/missing.mp3") == NULL);
    CHECK(decoderOpen(TMP_ROOT "/mono.txt") == NULL);
    decoderClose(NULL);
}

static void touch(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f)
        fclose(f);
}

static void makeTree(void)
{
    mkdir(TMP_ROOT "/lib", 0777);
    mkdir(TMP_ROOT "/lib/B sub", 0777);
    mkdir(TMP_ROOT "/lib/a sub", 0777);
    touch(TMP_ROOT "/lib/z.mp3");
    touch(TMP_ROOT "/lib/C.flac");
    touch(TMP_ROOT "/lib/cover.jpg");
    touch(TMP_ROOT "/lib/._hidden.mp3");
    touch(TMP_ROOT "/lib/B sub/x.ogg");
    touch(TMP_ROOT "/lib/a sub/y.wav");
}

static void testLibrary(void)
{
    TrackList list;
    CHECK(libraryScan(&list, TMP_ROOT "/lib", false));
    CHECK(list.count == 2);
    if (list.count == 2) {
        CHECK(strcmp(utilFileName(list.paths[0]), "C.flac") == 0);
        CHECK(strcmp(utilFileName(list.paths[1]), "z.mp3") == 0);
    }
    trackListFree(&list);

    CHECK(libraryScan(&list, TMP_ROOT "/lib", true));
    CHECK(list.count == 4);
    CHECK(trackListFind(&list, TMP_ROOT "/lib/B sub/x.ogg") < list.count);
    CHECK(trackListFind(&list, TMP_ROOT "/lib/nope.mp3") == list.count);
    trackListFree(&list);

    // System folders are skipped only right under a device root.
    CHECK(libraryIsSystemFolder("sdmc:/", "switch"));
    CHECK(libraryIsSystemFolder("sdmc:/", "Nintendo"));
    CHECK(libraryIsSystemFolder("sdmc:/", "CONFIG"));
    CHECK(!libraryIsSystemFolder("sdmc:/", "Music"));
    CHECK(!libraryIsSystemFolder("sdmc:/Music", "Game"));
    CHECK(!libraryIsSystemFolder(TMP_ROOT, "switch"));

    CHECK(libraryScan(&list, TMP_ROOT "/does-not-exist", true));
    CHECK(list.count == 0);
    trackListFree(&list);
}

static void testBrowser(void)
{
    Browser b = {0};
    CHECK(browserOpen(&b, TMP_ROOT "/lib"));
    CHECK(b.count == 4);  // 2 folders + 2 audio files; jpg and hidden skipped.
    if (b.count == 4) {
        CHECK(b.entries[0].is_dir && strcmp(b.entries[0].name, "a sub") == 0);
        CHECK(b.entries[1].is_dir && strcmp(b.entries[1].name, "B sub") == 0);
        CHECK(!b.entries[2].is_dir && strcmp(b.entries[2].name, "C.flac") == 0);
    }

    browserMove(&b, -5);
    CHECK(b.cursor == 0);
    browserMove(&b, 100);
    CHECK(b.cursor == 3);

    CHECK(browserOpen(&b, TMP_ROOT "/lib/B sub"));
    CHECK(b.count == 1);
    CHECK(browserGoUp(&b));
    CHECK(strcmp(b.path, TMP_ROOT "/lib") == 0);
    CHECK(b.cursor == 1);  // Back on "B sub".

    CHECK(!browserOpen(&b, TMP_ROOT "/nope"));
    CHECK(strcmp(b.path, TMP_ROOT "/lib") == 0);  // Listing kept.
    CHECK(browserSelected(&b) != NULL);
    browserFree(&b);
    CHECK(browserSelected(&b) == NULL);
}

int main(void)
{
    if (system("rm -rf " TMP_ROOT) != 0 || mkdir(TMP_ROOT, 0777) != 0) {
        fprintf(stderr, "cannot create %s\n", TMP_ROOT);
        return 2;
    }
    makeTree();

    testUtil();
    testQueueSequential();
    testQueueShuffle();
    testResampler();
    testDecoder();
    testLibrary();
    testBrowser();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
