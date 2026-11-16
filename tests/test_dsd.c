// Tests for DSF and DSDIFF playback (fixtures/make_dsd.py makes the files:
// 0.2 s of DSD64, 440 Hz left and 880 Hz right at half scale).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "decoder.h"
#include "dsd.h"
#include "tags.h"

#define FRAMES 17640  // 0.2 s at 88.2 kHz.
#define SETTLE 600    // The filter's start, left out of the measurements.

static float *decodeAll(const char *path, uint64_t *frames)
{
    Decoder *d = decoderOpen(path);
    CHECK(d != NULL);
    if (!d)
        return NULL;
    CHECK(decoderSampleRate(d) == 88200 && decoderTotalFrames(d) == FRAMES);
    float *pcm = calloc(FRAMES * 2 + 2048, sizeof(float));
    uint64_t total = 0, got;
    while (pcm && (got = decoderReadStereo(d, pcm + total * 2, 1000)) > 0)
        total += got;
    *frames = total;
    decoderClose(d);
    return pcm;
}

// The tone's level (RMS) and pitch (from zero crossings) in one channel.
static void measure(const float *pcm, uint64_t frames, int channel, double *rms, double *hz)
{
    double sum = 0.0;
    int crossings = 0;
    for (uint64_t i = SETTLE; i < frames; i++) {
        float v = pcm[i * 2 + channel], prev = pcm[(i - 1) * 2 + channel];
        sum += (double)v * v;
        crossings += (prev < 0.0f) != (v < 0.0f);
    }
    double seconds = (double)(frames - SETTLE) / 88200.0;
    *rms = sqrt(sum / (double)(frames - SETTLE));
    *hz = crossings / 2.0 / seconds;
}

static void checkTags(const char *path)
{
    TrackTags t;
    tagsInit(&t);
    tagsRead(path, &t);
    CHECK(strcmp(t.title, "DSD Tone") == 0);
    CHECK(strcmp(t.artist, "T\xC3\xABst \xC3\x81rtist") == 0);
    CHECK(strcmp(t.album, "Fixture Album") == 0);
    tagsFree(&t);
}

static void checkRejects(void)
{
    const char *path = TMP_ROOT "/bad.dff";
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    // A DSDIFF form whose audio is DST-compressed.
    static const uint8_t dst[] = {'F', 'R', 'M', '8', 0, 0, 0, 0, 0, 0, 0, 16, 'D', 'S', 'D', ' ',
                                  'D', 'S', 'T', ' ', 0, 0, 0, 0, 0, 0, 0, 0};
    fwrite(dst, 1, sizeof(dst), f);
    fclose(f);
    CHECK(decoderOpen(path) == NULL);
    f = fopen(path, "wb");
    fputs("not a DSD file at all", f);
    fclose(f);
    CHECK(decoderOpen(path) == NULL);
}

void testDsd(void)
{
    uint64_t dsf_frames = 0, dff_frames = 0;
    float *dsf = decodeAll(FIXTURES "/tone.dsf", &dsf_frames);
    float *dff = decodeAll(FIXTURES "/tone.dff", &dff_frames);
    CHECK(dsf_frames == FRAMES && dff_frames == FRAMES);
    if (dsf && dff && dsf_frames == FRAMES && dff_frames == FRAMES) {
        double rms, hz;
        measure(dsf, FRAMES, 0, &rms, &hz);
        CHECK(fabs(hz - 440.0) < 6.0);
        CHECK(rms > 0.32 && rms < 0.39);  // A half-scale sine: 0.354.
        measure(dsf, FRAMES, 1, &rms, &hz);
        CHECK(fabs(hz - 880.0) < 6.0);
        float diff = 0.0f;  // The same bits, stored the other way: the same sound.
        for (int i = 0; i < FRAMES * 2; i++)
            diff = fmaxf(diff, fabsf(dsf[i] - dff[i]));
        CHECK(diff < 1e-6f);

        Decoder *d = decoderOpen(FIXTURES "/tone.dsf");
        static float part[200 * 2];
        CHECK(d && decoderSeek(d, 8820) && decoderReadStereo(d, part, 200) == 200);
        CHECK(d && memcmp(part, dsf + 8820 * 2, sizeof(part)) == 0);  // Seeking lands exactly.
        CHECK(d && decoderSeek(d, FRAMES + 5) && decoderReadStereo(d, part, 200) == 0);
        decoderClose(d);
    }
    free(dsf);
    free(dff);
    checkTags(FIXTURES "/tone.dsf");
    checkTags(FIXTURES "/tone.dff");
    checkRejects();
}
