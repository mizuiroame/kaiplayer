// AAC, ALAC and Opus through FFmpeg (ffaudio.c): KaiPlayer's decoder plays
// the samples FFmpeg gives decoding the file directly, seeks land on the
// right frames, the formats read as Now Playing shows them, and the tags,
// ReplayGain and covers come out as for FLAC. Over the "network" too: that
// part is in test_kfile.c, with the other formats.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

#include "check.h"
#include "decoder.h"
#include "ffaudio.h"
#include "kfile.h"
#include "tags.h"

#define MAX_FRAMES (4 * 96000)
#define SEEK_FRAMES 400
#define AVIO_SIZE 32768

// --- FFmpeg on its own, opening the file itself ---

// Converts and appends one frame. Mono stays mono (doubled after), as in ffaudio.c.
static void appendFrame(SwrContext *swr, const AVFrame *f, int channels, float *out, uint64_t *frames)
{
    uint8_t *to[1] = {(uint8_t *)(out + *frames * (uint64_t)channels)};
    int room = (int)(MAX_FRAMES - *frames);
    int n = swr_convert(swr, to, room, (const uint8_t **)f->extended_data, f->nb_samples);
    if (n > 0)
        *frames += (uint64_t)n;
}

// The build has no file protocol (KaiPlayer gives FFmpeg its reads): plain stdio here, not kfile.
static int stdioRead(void *opaque, uint8_t *buf, int size)
{
    size_t n = fread(buf, 1, (size_t)size, opaque);
    return n ? (int)n : AVERROR_EOF;
}

static int64_t stdioSeek(void *opaque, int64_t offset, int whence)
{
    FILE *f = opaque;
    if (whence == AVSEEK_SIZE) {
        long here = ftell(f);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, here, SEEK_SET);
        return size;
    }
    return fseek(f, (long)offset, whence & ~AVSEEK_FORCE) == 0 ? ftell(f) : -1;
}

static AVFormatContext *openDirect(FILE *file)
{
    AVFormatContext *fc = avformat_alloc_context();
    AVIOContext *pb = avio_alloc_context(av_malloc(AVIO_SIZE), AVIO_SIZE, 0, file, stdioRead, NULL, stdioSeek);
    fc->pb = pb;
    fc->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&fc, NULL, NULL, NULL) == 0)
        return fc;
    av_freep(&pb->buffer);
    avio_context_free(&pb);
    return NULL;
}

static void closeDirect(AVFormatContext *fc, AVIOContext *pb, FILE *file)
{
    avformat_close_input(&fc);
    if (pb)
        av_freep(&pb->buffer);
    avio_context_free(&pb);
    fclose(file);
}

// The file decoded by FFmpeg on its own, to interleaved stereo float.
static float *decodeDirect(const char *path, uint64_t *frames)
{
    *frames = 0;
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    AVFormatContext *fc = openDirect(file);
    if (!fc) {
        fclose(file);
        return NULL;
    }
    AVIOContext *pb = fc->pb;
    const AVCodec *codec = NULL;
    int stream = avformat_find_stream_info(fc, NULL) >= 0
                     ? av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0) : -1;
    AVCodecContext *c = stream >= 0 ? avcodec_alloc_context3(codec) : NULL;
    if (!c || avcodec_parameters_to_context(c, fc->streams[stream]->codecpar) < 0 ||
        avcodec_open2(c, codec, NULL) != 0) {
        avcodec_free_context(&c);
        closeDirect(fc, pb, file);
        return NULL;
    }
    int channels = c->ch_layout.nb_channels == 1 ? 1 : 2;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, channels);
    SwrContext *swr = NULL;
    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, c->sample_rate, &c->ch_layout, c->sample_fmt,
                        c->sample_rate, 0, NULL);
    swr_init(swr);
    float *native = malloc((size_t)MAX_FRAMES * 2 * sizeof(float));
    AVPacket *p = av_packet_alloc();
    AVFrame *f = av_frame_alloc();
    bool more = true;
    while (more) {
        more = av_read_frame(fc, p) >= 0;
        if (more && p->stream_index != stream) {
            av_packet_unref(p);
            continue;
        }
        avcodec_send_packet(c, more ? p : NULL);
        av_packet_unref(p);
        while (avcodec_receive_frame(c, f) == 0) {
            appendFrame(swr, f, channels, native, frames);
            av_frame_unref(f);
        }
    }
    float *stereo = malloc((size_t)*frames * 2 * sizeof(float) + 1);
    for (uint64_t i = 0; i < *frames; i++) {
        stereo[i * 2] = native[i * (uint64_t)channels];
        stereo[i * 2 + 1] = native[i * (uint64_t)channels + (uint64_t)channels - 1];
    }
    free(native);
    av_frame_free(&f);
    av_packet_free(&p);
    swr_free(&swr);
    avcodec_free_context(&c);
    closeDirect(fc, pb, file);
    return stereo;
}

static float *decodeAll(const char *path, uint64_t *frames)
{
    *frames = 0;
    Decoder *d = decoderOpen(path);
    if (!d)
        return NULL;
    float *out = malloc((size_t)MAX_FRAMES * 2 * sizeof(float));
    uint64_t got;
    while (*frames < MAX_FRAMES - 1000 && (got = decoderReadStereo(d, out + *frames * 2, 1000)) > 0)
        *frames += got;
    decoderClose(d);
    return out;
}

static float maxDifference(const float *a, const float *b, size_t samples)
{
    float worst = 0.0f;
    for (size_t i = 0; i < samples; i++)
        worst = fmaxf(worst, fabsf(a[i] - b[i]));
    return worst;
}

// The label, but with an estimated bit rate (Ogg and ADTS store none: it is the
// file's size over its length) within a tenth below, half above for these
// 1-second files, whose headers weigh.
static bool sameLabel(const char *text, const char *label, bool estimated)
{
    if (!estimated)
        return strcmp(text, label) == 0;
    const char *a = strstr(text, " kbps"), *b = strstr(label, " kbps");
    if (!a || !b)
        return false;
    while (a > text && a[-1] != ' ')
        a--;
    while (b > label && b[-1] != ' ')
        b--;
    double got = atof(a), want = atof(b);
    return a - text == b - label && !strncmp(text, label, (size_t)(a - text)) && got > want * 0.9 && got < want * 1.5;
}

// label: what Now Playing shows. tolerance: how far a seek's samples may be
// from the ones decoded from the start. 0 for ALAC; a lossy decoder starting
// cold settles within its preroll.
static void checkFile(const char *name, const char *label, bool estimated, float tolerance)
{
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "/%s", name);
    uint64_t frames = 0, direct_frames = 0;
    float *ours = decodeAll(path, &frames);
    float *direct = decodeDirect(path, &direct_frames);
    CHECK(ours && direct && frames > 10000);
    CHECK(frames == direct_frames && ours && direct && !memcmp(ours, direct, (size_t)frames * 2 * sizeof(float)));

    Decoder *d = decoderOpen(path);
    CHECK(d != NULL);
    if (d && ours) {
        AudioFormat f;
        decoderFormat(d, &f);
        char text[64];
        decoderFormatLabel(&f, text, sizeof(text));
        CHECK(sameLabel(text, label, estimated));
        if (!sameLabel(text, label, estimated))
            fprintf(stderr, "  %s: \"%s\", not \"%s\"\n", name, text, label);
        // The length from the stream's duration is what plays, near enough (an ADTS file's is estimated).
        uint64_t total = decoderTotalFrames(d);
        CHECK(total > 0 && fabs((double)total - (double)frames) < decoderSampleRate(d) * 0.05);
        // Seeks, forward and back, land on the frames decoded from the start.
        static const double PLACES[] = {0.5, 0.1, 0.8, 0.0};
        for (size_t i = 0; i < sizeof(PLACES) / sizeof(PLACES[0]); i++) {
            uint64_t at = (uint64_t)(frames * PLACES[i]) + (i ? 37 : 0);
            float part[SEEK_FRAMES * 2];
            bool ok = decoderSeek(d, at) && decoderReadStereo(d, part, SEEK_FRAMES) == SEEK_FRAMES;
            CHECK(ok);
            float diff = ok ? maxDifference(part, ours + at * 2, SEEK_FRAMES * 2) : 1.0f;
            CHECK(diff <= tolerance);
            if (diff > tolerance)
                fprintf(stderr, "  %s: a seek to %llu is %g away\n", name, (unsigned long long)at, diff);
        }
        float part[16];
        CHECK(decoderSeek(d, frames + 1000) && decoderReadStereo(d, part, 8) == 0);  // Past the end: nothing.
    }
    decoderClose(d);
    free(ours);
    free(direct);
}

// The tone is a quiet sine (lavfi's), the same on both channels: a lossy coder keeps them within a hair.
static void checkTone(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "/%s", name);
    uint64_t frames = 0;
    float *s = decodeAll(path, &frames);
    double sum = 0.0, side = 0.0;
    for (uint64_t i = frames / 4; s && i < frames * 3 / 4; i++) {
        sum += (double)s[i * 2] * s[i * 2];
        side = fmax(side, fabs((double)s[i * 2] - s[i * 2 + 1]));
    }
    double rms = frames ? sqrt(sum / (double)(frames / 2)) : 0.0;
    CHECK(rms > 0.05 && rms < 0.6 && side < 1e-3);
    if (!(rms > 0.05 && rms < 0.6 && side < 1e-3))
        fprintf(stderr, "  %s: rms %g, left and right %g apart\n", name, rms, side);
    free(s);
}

static void testDecoding(void)
{
    checkFile("tone.m4a", "AAC \xC2\xB7 128 kbps", false, 1e-3f);
    checkFile("tone_alac.m4a", "ALAC \xC2\xB7 24-bit \xC2\xB7 96 kHz", false, 0.0f);
    checkFile("tone.opus", "Opus \xC2\xB7 96 kbps", true, 2e-2f);
    checkFile("tone_opus.ogg", "Opus \xC2\xB7 48 kbps", true, 2e-2f);
    checkFile("tone.aac", "AAC \xC2\xB7 96 kbps", true, 1e-3f);
    checkTone("tone.m4a");
    checkTone("tone_alac.m4a");
    checkTone("tone.opus");
    checkTone("tone_opus.ogg");  // Mono, on both sides.

    // Vorbis in .ogg still goes to stb_vorbis; Opus in .ogg to FFmpeg.
    KFile *f = kfileOpen(FIXTURES "/tone.ogg", KFILE_PEEK);
    CHECK(f && !ffaudioOggIsOpus(f) && kfileTell(f) == 0);
    kfileClose(f);
    f = kfileOpen(FIXTURES "/tone_opus.ogg", KFILE_PEEK);
    CHECK(f && ffaudioOggIsOpus(f) && kfileTell(f) == 0);
    kfileClose(f);
    CHECK(ffaudioIsFile("a/B.M4A") && ffaudioIsFile("x.opus") && ffaudioIsFile("x.aac") && ffaudioIsFile("x.mp4"));
    CHECK(!ffaudioIsFile("x.ogg") && !ffaudioIsFile("x.flac") && !ffaudioIsFile("m4a"));
}

// --- Tags ---

static uint8_t *readFile(const char *path, size_t *size)
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

static bool sameCover(const TrackTags *t)
{
    size_t size = 0;
    uint8_t *want = readFile(FIXTURES "/small_cover.jpg", &size);
    bool same = want && t->picture && t->picture_size == size && !memcmp(t->picture, want, size);
    free(want);
    return same && t->picture_type == 3;
}

// tone.m4a and tone.opus carry the same tags; their ReplayGain differs.
static void checkFullTags(const char *name, float track_gain, bool album_gain)
{
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "/%s", name);
    TrackTags t;
    tagsInit(&t);
    CHECK(tagsRead(path, &t));
    CHECK(!strcmp(t.title, "Test Title") && !strcmp(t.artist, "T\xC3\xABst \xC3\x81rtist"));
    CHECK(!strcmp(t.album, "Fixture Album") && !strcmp(t.album_artist, "Fixture Band"));
    CHECK(t.track_number == 3 && t.disc_number == 1 && t.year == 2021);
    CHECK(t.has_track_gain && fabsf(t.track_gain_db - track_gain) < 0.01f);
    CHECK(t.has_album_gain == album_gain && (!album_gain || fabsf(t.album_gain_db + 7.25f) < 0.01f));
    CHECK(sameCover(&t));
    tagsFree(&t);

    tagsInit(&t);
    t.skip_picture = true;  // ReplayGain's read: the tags, no picture.
    CHECK(tagsRead(path, &t) && !t.picture && !strcmp(t.title, "Test Title"));
    tagsFree(&t);
}

static void testTagsRead(void)
{
    checkFullTags("tone.m4a", -6.5f, true);   // iTunes "----" atoms.
    checkFullTags("tone.opus", -5.0f, false); // R128_TRACK_GAIN -2560: -10 dB to -23 LUFS, -5 dB as ReplayGain.

    TrackTags t;
    tagsInit(&t);
    CHECK(tagsRead(FIXTURES "/tone_alac.m4a", &t));
    CHECK(!strcmp(t.title, "ALAC Tone") && t.track_number == 4 && !t.picture && !t.has_track_gain);
    tagsFree(&t);
    tagsInit(&t);
    CHECK(tagsRead(FIXTURES "/tone_opus.ogg", &t) && !strcmp(t.title, "Opus in Ogg"));
    tagsFree(&t);
    tagsInit(&t);
    CHECK(tagsRead(FIXTURES "/tone.ogg", &t) && !strcmp(t.title, "Test Title"));  // Vorbis: as before.
    tagsFree(&t);
    tagsInit(&t);
    CHECK(tagsRead(FIXTURES "/tone.aac", &t) && !t.title[0]);  // No tags in it.
    tagsFree(&t);
}

// --- Damaged files ---

static void writeFile(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(data, 1, size, f);
    fclose(f);
}

static void testDamaged(void)
{
    static const char *const NAMES[] = {"tone.m4a", "tone_alac.m4a", "tone.opus", "tone_opus.ogg"};
    float buf[1000 * 2];
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        char path[256], cut[256];
        snprintf(path, sizeof(path), FIXTURES "/%s", NAMES[i]);
        snprintf(cut, sizeof(cut), TMP_ROOT "/cut_%s", NAMES[i]);
        size_t size = 0;
        uint8_t *data = readFile(path, &size);
        if (!data)
            continue;
        // Cut in half; then its middle overwritten.
        for (int pass = 0; pass < 2; pass++) {
            if (pass)
                memset(data + size / 3, 0x5A, size / 3);
            writeFile(cut, data, pass ? size : size / 2);
            Decoder *d = decoderOpen(cut);
            uint64_t got = 0, n;
            while (d && got < MAX_FRAMES && (n = decoderReadStereo(d, buf, 1000)) > 0)
                got += n;
            if (d)
                decoderSeek(d, 1000);
            decoderClose(d);
            TrackTags t;
            tagsInit(&t);
            tagsRead(cut, &t);
            tagsFree(&t);
        }
        free(data);
    }
    writeFile(TMP_ROOT "/junk.m4a", (const uint8_t *)"not a song", 10);
    CHECK(decoderOpen(TMP_ROOT "/junk.m4a") == NULL);
    TrackTags t;
    tagsInit(&t);
    CHECK(tagsRead(TMP_ROOT "/junk.m4a", &t) && !t.title[0]);  // Read, nothing in it.
    tagsFree(&t);
    CHECK(decoderOpen(TMP_ROOT "/missing.opus") == NULL);
    CHECK(!tagsRead(TMP_ROOT "/missing.opus", &t));
}

void testFfaudio(void)
{
    testDecoding();
    testTagsRead();
    testDamaged();
}
