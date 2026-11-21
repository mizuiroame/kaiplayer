#include "decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codecs_config.h"
#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"
#include "dsd.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "kfile.h"
#include "util.h"

#define MAX_CHANNELS 8
#define SCRATCH_FRAMES 1024
#define MIN_SAMPLE_RATE 1000
#define MAX_SAMPLE_RATE 768000
// stb_vorbis can't read through callbacks: a network Ogg file is read whole
// into memory first (Ogg songs are small).
#define VORBIS_MEMORY_MAX (64u << 20)

typedef enum { KIND_WAV, KIND_FLAC, KIND_MP3, KIND_VORBIS, KIND_DSD } DecoderKind;

struct Decoder {
    DecoderKind kind;
    KFile *file;            // What the dr_libs and stb_vorbis read (not DSD: dsd.c opens its own).
    uint8_t *memory;        // A network Ogg file, whole.
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

// The dr_libs read the file through kfile, on the card or the network.
static size_t onRead(void *user, void *to, size_t size)
{
    return kfileRead(user, to, size);
}

static int onSeekTo(void *user, int offset, int origin)  // Origins: SET, CUR, END, as stdio's.
{
    return kfileSeek(user, offset, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END);
}

static drflac_bool32 onSeekFlac(void *user, int offset, drflac_seek_origin origin)
{
    return onSeekTo(user, offset, (int)origin);
}

static drmp3_bool32 onSeekMp3(void *user, int offset, drmp3_seek_origin origin)
{
    return onSeekTo(user, offset, (int)origin);
}

static drwav_bool32 onSeekWav(void *user, int offset, drwav_seek_origin origin)
{
    return onSeekTo(user, offset, (int)origin);
}

static drflac_bool32 onTellFlac(void *user, drflac_int64 *at)
{
    *at = kfileTell(user);
    return *at >= 0;
}

static drmp3_bool32 onTellMp3(void *user, drmp3_int64 *at)
{
    *at = kfileTell(user);
    return *at >= 0;
}

static drwav_bool32 onTellWav(void *user, drwav_int64 *at)
{
    *at = kfileTell(user);
    return *at >= 0;
}

static bool openWav(Decoder *d)
{
    if (!drwav_init(&d->u.wav, onRead, onSeekWav, onTellWav, d->file, NULL))
        return false;
    d->channels = d->u.wav.channels;
    d->sample_rate = d->u.wav.sampleRate;
    d->total_frames = d->u.wav.totalPCMFrameCount;
    return true;
}

static bool openFlac(Decoder *d)
{
    d->u.flac = drflac_open(onRead, onSeekFlac, onTellFlac, d->file, NULL);
    if (!d->u.flac)
        return false;
    d->channels = d->u.flac->channels;
    d->sample_rate = d->u.flac->sampleRate;
    d->total_frames = d->u.flac->totalPCMFrameCount;
    return true;
}

static bool openMp3(Decoder *d)
{
    if (!drmp3_init(&d->u.mp3, onRead, onSeekMp3, onTellMp3, NULL, d->file, NULL))
        return false;
    d->channels = d->u.mp3.channels;
    d->sample_rate = d->u.mp3.sampleRate;
    // Known from a Xing/Info tag. Counting frames would read the whole file.
    uint64_t total = d->u.mp3.totalPCMFrameCount;
    d->total_frames = total == DRMP3_UINT64_MAX ? 0 : total;
    return true;
}

// A network file, whole, read as a stream: on a connection of its own, and
// given up when the song changes (kfileInterruptStreams). NULL if it can't be.
static uint8_t *readWhole(KFile *f, size_t *size)
{
    int64_t total = kfileSize(f);
    uint8_t *data = total > 0 && total <= VORBIS_MEMORY_MAX ? malloc((size_t)total) : NULL;
    if (data && kfileRead(f, data, (size_t)total) != (size_t)total) {
        free(data);
        data = NULL;
    }
    *size = data ? (size_t)total : 0;
    return data;
}

// A card file through its FILE (as stb_vorbis_open_filename does); a network one from memory.
static bool openVorbis(Decoder *d)
{
    int error = 0;
    if (kfileStdio(d->file)) {
        d->u.vorbis = stb_vorbis_open_file(kfileStdio(d->file), 0, &error, NULL);
    } else {
        size_t size = 0;
        d->memory = readWhole(d->file, &size);
        kfileClose(d->file);  // Read: its connection can go.
        d->file = NULL;
        if (d->memory)
            d->u.vorbis = stb_vorbis_open_memory(d->memory, (int)size, &error, NULL);
    }
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

// The kind of file, by its extension. False if no decoder reads it.
static bool kindOf(const char *path, DecoderKind *kind)
{
    if (utilHasExtension(path, "wav"))
        *kind = KIND_WAV;
    else if (utilHasExtension(path, "flac"))
        *kind = KIND_FLAC;
    else if (utilHasExtension(path, "mp3"))
        *kind = KIND_MP3;
    else if (utilHasExtension(path, "ogg") || utilHasExtension(path, "oga"))
        *kind = KIND_VORBIS;
    else if (utilHasExtension(path, "dsf") || utilHasExtension(path, "dff"))
        *kind = KIND_DSD;
    else
        return false;
    return true;
}

static void closeFiles(Decoder *d)
{
    kfileClose(d->file);
    free(d->memory);
}

Decoder *decoderOpen(const char *path)
{
    Decoder *d = calloc(1, sizeof(Decoder));
    if (!d)
        return NULL;
    bool ok = kindOf(path, &d->kind);
    if (ok && d->kind != KIND_DSD) {  // dsd.c opens its own.
        d->file = kfileOpen(path, KFILE_STREAM);
        ok = d->file != NULL;
    }
    if (ok) {
        switch (d->kind) {
        case KIND_WAV: ok = openWav(d); break;
        case KIND_FLAC: ok = openFlac(d); break;
        case KIND_MP3: ok = openMp3(d); break;
        case KIND_VORBIS: ok = openVorbis(d); break;
        case KIND_DSD: ok = openDsd(d, path); break;
        }
    }

    // Headers come from untrusted files: reject values playback can't handle.
    if (ok && (d->channels == 0 || d->channels > MAX_CHANNELS ||
               d->sample_rate < MIN_SAMPLE_RATE || d->sample_rate > MAX_SAMPLE_RATE)) {
        decoderClose(d);
        return NULL;
    }
    if (!ok) {
        closeFiles(d);
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
    closeFiles(d);  // After the decoder: it may read on closing.
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

void decoderFormat(const Decoder *d, AudioFormat *out)
{
    *out = (AudioFormat){"", d->sample_rate, 0};
    switch (d->kind) {
    case KIND_WAV:
        snprintf(out->codec, sizeof(out->codec), "WAV");
        out->bits = d->u.wav.bitsPerSample;
        break;
    case KIND_FLAC:
        snprintf(out->codec, sizeof(out->codec), "FLAC");
        out->bits = d->u.flac->bitsPerSample;
        break;
    case KIND_MP3: snprintf(out->codec, sizeof(out->codec), "MP3"); break;
    case KIND_VORBIS: snprintf(out->codec, sizeof(out->codec), "OGG"); break;
    case KIND_DSD: {
        out->sample_rate = dsdRate(d->u.dsd);
        unsigned base = out->sample_rate % 3072000u == 0 ? 3072000u / 64 : 2822400u / 64;  // 48 or 44.1 kHz.
        snprintf(out->codec, sizeof(out->codec), "DSD%u", out->sample_rate / base);
        break;
    }
    }
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
