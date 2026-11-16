#include "decoder.h"

#include <stdlib.h>
#include <string.h>

#include "codecs_config.h"
#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"
#include "dsd.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "util.h"

#define MAX_CHANNELS 8
#define SCRATCH_FRAMES 1024
#define MIN_SAMPLE_RATE 1000
#define MAX_SAMPLE_RATE 768000

typedef enum { KIND_WAV, KIND_FLAC, KIND_MP3, KIND_VORBIS, KIND_DSD } DecoderKind;

struct Decoder {
    DecoderKind kind;
    uint32_t channels;
    uint32_t sample_rate;
    uint64_t total_frames;
    union {
        drwav wav;
        drflac *flac;
        drmp3 mp3;
        stb_vorbis *vorbis;
        DsdDecoder *dsd;
    } u;
    float scratch[SCRATCH_FRAMES * MAX_CHANNELS];
};

static bool openWav(Decoder *d, const char *path)
{
    if (!drwav_init_file(&d->u.wav, path, NULL))
        return false;
    d->channels = d->u.wav.channels;
    d->sample_rate = d->u.wav.sampleRate;
    d->total_frames = d->u.wav.totalPCMFrameCount;
    return true;
}

static bool openFlac(Decoder *d, const char *path)
{
    d->u.flac = drflac_open_file(path, NULL);
    if (!d->u.flac)
        return false;
    d->channels = d->u.flac->channels;
    d->sample_rate = d->u.flac->sampleRate;
    d->total_frames = d->u.flac->totalPCMFrameCount;
    return true;
}

static bool openMp3(Decoder *d, const char *path)
{
    if (!drmp3_init_file(&d->u.mp3, path, NULL))
        return false;
    d->channels = d->u.mp3.channels;
    d->sample_rate = d->u.mp3.sampleRate;
    // Known from a Xing/Info tag. Counting frames would read the whole file.
    uint64_t total = d->u.mp3.totalPCMFrameCount;
    d->total_frames = total == DRMP3_UINT64_MAX ? 0 : total;
    return true;
}

static bool openVorbis(Decoder *d, const char *path)
{
    int error = 0;
    d->u.vorbis = stb_vorbis_open_filename(path, &error, NULL);
    if (!d->u.vorbis)
        return false;
    stb_vorbis_info info = stb_vorbis_get_info(d->u.vorbis);
    d->channels = (uint32_t)info.channels;
    d->sample_rate = info.sample_rate;
    d->total_frames = stb_vorbis_stream_length_in_samples(d->u.vorbis);
    return true;
}

static bool openDsd(Decoder *d, const char *path)
{
    d->u.dsd = dsdOpen(path);
    if (!d->u.dsd)
        return false;
    d->channels = dsdChannels(d->u.dsd);
    d->sample_rate = dsdSampleRate(d->u.dsd);
    d->total_frames = dsdTotalFrames(d->u.dsd);
    return true;
}

Decoder *decoderOpen(const char *path)
{
    Decoder *d = calloc(1, sizeof(Decoder));
    if (!d)
        return NULL;

    bool ok = false;
    if (utilHasExtension(path, "wav")) {
        d->kind = KIND_WAV;
        ok = openWav(d, path);
    } else if (utilHasExtension(path, "flac")) {
        d->kind = KIND_FLAC;
        ok = openFlac(d, path);
    } else if (utilHasExtension(path, "mp3")) {
        d->kind = KIND_MP3;
        ok = openMp3(d, path);
    } else if (utilHasExtension(path, "ogg") || utilHasExtension(path, "oga")) {
        d->kind = KIND_VORBIS;
        ok = openVorbis(d, path);
    } else if (utilHasExtension(path, "dsf") || utilHasExtension(path, "dff")) {
        d->kind = KIND_DSD;
        ok = openDsd(d, path);
    }

    // Headers come from untrusted files: reject values playback can't handle.
    if (ok && (d->channels == 0 || d->channels > MAX_CHANNELS ||
               d->sample_rate < MIN_SAMPLE_RATE || d->sample_rate > MAX_SAMPLE_RATE)) {
        decoderClose(d);
        return NULL;
    }
    if (!ok) {
        free(d);
        return NULL;
    }
    return d;
}

void decoderClose(Decoder *d)
{
    if (!d)
        return;
    switch (d->kind) {
    case KIND_WAV: drwav_uninit(&d->u.wav); break;
    case KIND_FLAC: drflac_close(d->u.flac); break;
    case KIND_MP3: drmp3_uninit(&d->u.mp3); break;
    case KIND_VORBIS: stb_vorbis_close(d->u.vorbis); break;
    case KIND_DSD: dsdClose(d->u.dsd); break;
    }
    free(d);
}

uint32_t decoderSampleRate(const Decoder *d)
{
    return d->sample_rate;
}

uint64_t decoderTotalFrames(const Decoder *d)
{
    return d->total_frames;
}

static uint64_t readNative(Decoder *d, float *out, uint64_t frames)
{
    switch (d->kind) {
    case KIND_WAV: return drwav_read_pcm_frames_f32(&d->u.wav, frames, out);
    case KIND_FLAC: return drflac_read_pcm_frames_f32(d->u.flac, frames, out);
    case KIND_MP3: return drmp3_read_pcm_frames_f32(&d->u.mp3, frames, out);
    case KIND_VORBIS:
        return (uint64_t)stb_vorbis_get_samples_float_interleaved(
            d->u.vorbis, (int)d->channels, out, (int)(frames * d->channels));
    case KIND_DSD: return dsdRead(d->u.dsd, out, frames);
    }
    return 0;
}

// Mono is duplicated; more than two channels keep front left and right.
static void toStereo(const float *in, uint32_t channels, float *out, uint64_t frames)
{
    for (uint64_t i = 0; i < frames; i++) {
        const float *f = in + i * channels;
        out[i * 2] = f[0];
        out[i * 2 + 1] = channels > 1 ? f[1] : f[0];
    }
}

uint64_t decoderReadStereo(Decoder *d, float *out, uint64_t frames)
{
    uint64_t total = 0;
    while (total < frames) {
        uint64_t want = frames - total;
        if (want > SCRATCH_FRAMES)
            want = SCRATCH_FRAMES;
        uint64_t got = readNative(d, d->scratch, want);
        if (got == 0)
            break;
        toStereo(d->scratch, d->channels, out + total * 2, got);
        total += got;
    }
    return total;
}

bool decoderSeek(Decoder *d, uint64_t frame)
{
    switch (d->kind) {
    case KIND_WAV: return drwav_seek_to_pcm_frame(&d->u.wav, frame);
    case KIND_FLAC: return drflac_seek_to_pcm_frame(d->u.flac, frame);
    case KIND_MP3: return drmp3_seek_to_pcm_frame(&d->u.mp3, frame);
    case KIND_VORBIS: return stb_vorbis_seek(d->u.vorbis, (unsigned)frame) != 0;
    case KIND_DSD: return dsdSeek(d->u.dsd, frame);
    }
    return false;
}
