#include "dsd.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DSD64_44K 2822400u    // 64 x 44.1 kHz.
#define DSD64_48K 3072000u    // 64 x 48 kHz.
#define MAX_MULTIPLE 8u        // DSD512.
#define DSF_BLOCK_MAX (1u << 20)
#define MAX_CHUNKS 256
#define CHUNK_FRAMES 1024
#define SILENCE 0x69           // Equal ones and zeros: a DSD pattern for silence.
#define CUTOFF_HZ 38000.0      // The filter passes the audio band and stops what would alias.
#define TAPS_PER_RATIO 16      // Filter length, in output-frame steps.

struct DsdDecoder {
    KFile *f;
    DsdInfo info;
    uint32_t out_rate;
    uint32_t step;    // Bytes per channel per PCM frame.
    uint32_t window;  // Bytes per channel the filter reads for one PCM frame.
    uint64_t total_frames;
    uint64_t frame;
    float *table;     // window x 256: each byte position's contribution, per byte value.
    uint8_t *raw;
    size_t raw_cap;
    uint8_t *chan;    // The bytes being filtered, one run per channel.
    size_t chan_cap;
};

static uint8_t g_reverse[256];

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return le32(p) | (uint64_t)le32(p + 4) << 32;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}

static bool readAt(KFile *f, uint64_t offset, void *buf, size_t size)
{
    return offset <= (uint64_t)INT64_MAX / 2 && kfileSeek(f, (int64_t)offset, SEEK_SET) &&
           kfileRead(f, buf, size) == size;
}

// DSD64 to DSD512, in the 44.1 or 48 kHz family.
static bool supportedRate(uint32_t rate)
{
    uint32_t base = rate % DSD64_44K == 0 ? DSD64_44K : rate % DSD64_48K == 0 ? DSD64_48K : 0;
    uint32_t multiple = base ? rate / base : 0;
    return multiple >= 1 && multiple <= MAX_MULTIPLE && (multiple & (multiple - 1)) == 0;
}

// DSF: a "DSD " chunk (with the ID3 pointer), "fmt " and "data", little-endian.
static bool readDsf(KFile *f, DsdInfo *info)
{
    uint8_t h[52];
    if (!readAt(f, 0, h, 28) || memcmp(h, "DSD ", 4) != 0)
        return false;
    uint64_t fmt_at = le64(h + 4);
    info->id3_offset = le64(h + 20);
    if (fmt_at < 28 || !readAt(f, fmt_at, h, 52) || memcmp(h, "fmt ", 4) != 0)
        return false;
    uint64_t fmt_size = le64(h + 4);
    uint32_t format = le32(h + 16), bits = le32(h + 32), block = le32(h + 44);
    uint64_t samples = le64(h + 36);
    info->channels = le32(h + 24);
    info->dsd_rate = le32(h + 28);
    info->block = block;
    info->lsb_first = bits == 1;
    if (fmt_size < 52 || fmt_size > (1u << 20) || format != 0 || (bits != 1 && bits != 8) || block == 0 ||
        block > DSF_BLOCK_MAX || info->channels == 0)
        return false;
    uint64_t data_at = fmt_at + fmt_size;
    if (!readAt(f, data_at, h, 12) || memcmp(h, "data", 4) != 0 || le64(h + 4) < 12)
        return false;
    info->data_offset = data_at + 12;
    // The sample count, unless the data chunk says there's less.
    uint64_t in_data = (le64(h + 4) - 12) / info->channels;
    info->bytes_per_channel = samples / 8 < in_data ? samples / 8 : in_data;
    return true;
}

// The PROP chunk's sample rate, channel count and compression.
static bool readDffProperties(KFile *f, uint64_t at, uint64_t end, DsdInfo *info)
{
    uint8_t h[12], v[4];
    if (!readAt(f, at, h, 4) || memcmp(h, "SND ", 4) != 0)
        return false;
    bool uncompressed = false;
    at += 4;
    for (int i = 0; i < MAX_CHUNKS && at + 12 <= end; i++) {
        if (!readAt(f, at, h, 12))
            return false;
        uint64_t size = be64(h + 4), body = at + 12;
        if (size > end - body)
            return false;
        if (memcmp(h, "FS  ", 4) == 0 && size >= 4 && readAt(f, body, v, 4))
            info->dsd_rate = be32(v);
        else if (memcmp(h, "CHNL", 4) == 0 && size >= 2 && readAt(f, body, v, 2))
            info->channels = (uint32_t)v[0] << 8 | v[1];
        else if (memcmp(h, "CMPR", 4) == 0 && size >= 4 && readAt(f, body, v, 4))
            uncompressed = memcmp(v, "DSD ", 4) == 0;
        at = body + size + (size & 1);
    }
    return uncompressed;
}

// DSDIFF: big-endian chunks in a "FRM8" form of type "DSD ".
static bool readDff(KFile *f, DsdInfo *info)
{
    uint8_t h[16];
    if (!readAt(f, 0, h, 16) || memcmp(h, "FRM8", 4) != 0 || memcmp(h + 12, "DSD ", 4) != 0 ||
        be64(h + 4) > (1ull << 48))
        return false;
    uint64_t end = be64(h + 4) + 12, at = 16;
    bool props = false, data = false;
    info->block = 1;
    info->lsb_first = false;
    for (int i = 0; i < MAX_CHUNKS && at + 12 <= end; i++) {
        if (!readAt(f, at, h, 12))
            break;  // A truncated file: use what was found.
        uint64_t size = be64(h + 4), body = at + 12;
        if (size > (1ull << 48))
            return false;
        if (memcmp(h, "PROP", 4) == 0)
            props = readDffProperties(f, body, body + size, info);
        else if (memcmp(h, "DST ", 4) == 0)
            return false;  // Compressed.
        else if (memcmp(h, "DSD ", 4) == 0) {
            info->data_offset = body;
            info->bytes_per_channel = size;  // Divided by the channels below.
            data = true;
        } else if (memcmp(h, "ID3 ", 4) == 0 || memcmp(h, "id3 ", 4) == 0)
            info->id3_offset = body;
        at = body + size + (size & 1);
    }
    if (!props || !data || info->channels == 0)
        return false;
    info->bytes_per_channel /= info->channels;
    return true;
}

bool dsdReadInfo(KFile *f, DsdInfo *info)
{
    memset(info, 0, sizeof(*info));
    bool ok = readDsf(f, info);
    if (!ok) {
        memset(info, 0, sizeof(*info));
        ok = readDff(f, info);
    }
    return ok && info->channels >= 1 && info->channels <= DSD_MAX_CHANNELS && supportedRate(info->dsd_rate) &&
           info->bytes_per_channel > 0;
}

// A windowed-sinc low-pass (Blackman), unity gain at DC, folded into a table
// so each input byte (8 samples) costs one lookup. Bit 7 is the earliest sample.
static float *makeTable(uint32_t window, double cutoff)
{
    uint32_t taps = window * 8;
    double *h = malloc(taps * sizeof(double));
    float *table = malloc((size_t)window * 256 * sizeof(float));
    if (!h || !table) {
        free(h);
        free(table);
        return NULL;
    }
    double sum = 0.0, mid = (taps - 1) / 2.0;
    for (uint32_t n = 0; n < taps; n++) {
        double x = n - mid, phase = 2.0 * M_PI * n / (taps - 1);
        double sinc = x == 0.0 ? 2.0 * cutoff : sin(2.0 * M_PI * cutoff * x) / (M_PI * x);
        h[n] = sinc * (0.42 - 0.5 * cos(phase) + 0.08 * cos(2.0 * phase));
        sum += h[n];
    }
    for (uint32_t k = 0; k < window; k++) {
        for (uint32_t b = 0; b < 256; b++) {
            double v = 0.0;
            for (uint32_t j = 0; j < 8; j++)
                v += h[k * 8 + j] * ((b >> (7 - j)) & 1 ? 1.0 : -1.0);
            table[k * 256 + b] = (float)(v / sum);
        }
    }
    free(h);
    return table;
}

static void initReverse(void)
{
    for (int b = 0; b < 256; b++) {
        int r = 0;
        for (int i = 0; i < 8; i++)
            r |= ((b >> i) & 1) << (7 - i);
        g_reverse[b] = (uint8_t)r;
    }
}

DsdDecoder *dsdOpen(const char *path)
{
    DsdDecoder *d = calloc(1, sizeof(DsdDecoder));
    if (!d)
        return NULL;
    d->f = kfileOpen(path, KFILE_STREAM);
    if (!d->f || !dsdReadInfo(d->f, &d->info)) {
        dsdClose(d);
        return NULL;
    }
    if (!g_reverse[1])
        initReverse();
    d->out_rate = d->info.dsd_rate % DSD64_44K == 0 ? 88200 : 96000;
    uint32_t ratio = d->info.dsd_rate / d->out_rate;  // 32 for DSD64.
    d->step = ratio / 8;
    d->window = ratio * TAPS_PER_RATIO / 8;
    d->total_frames = d->info.bytes_per_channel / d->step;
    d->table = makeTable(d->window, CUTOFF_HZ / d->info.dsd_rate);
    if (!d->table) {
        dsdClose(d);
        return NULL;
    }
    return d;
}

void dsdClose(DsdDecoder *d)
{
    if (!d)
        return;
    kfileClose(d->f);
    free(d->table);
    free(d->raw);
    free(d->chan);
    free(d);
}

uint32_t dsdChannels(const DsdDecoder *d)
{
    return d->info.channels;
}

uint32_t dsdSampleRate(const DsdDecoder *d)
{
    return d->out_rate;
}

uint32_t dsdRate(const DsdDecoder *d)
{
    return d->info.dsd_rate;
}

uint64_t dsdTotalFrames(const DsdDecoder *d)
{
    return d->total_frames;
}

static bool reserve(uint8_t **buf, size_t *cap, size_t size)
{
    if (size <= *cap)
        return true;
    uint8_t *grown = realloc(*buf, size);
    if (!grown)
        return false;
    *buf = grown;
    *cap = size;
    return true;
}

// Fills each channel's run with its bytes [first, end) (MSB first);
// before the start, or where the file is short, with silence.
static bool loadBytes(DsdDecoder *d, int64_t first, uint64_t end, size_t span)
{
    uint32_t channels = d->info.channels, block = d->info.block;
    if (!reserve(&d->chan, &d->chan_cap, span * channels))
        return false;
    memset(d->chan, SILENCE, span * channels);
    uint64_t start = first < 0 ? 0 : (uint64_t)first;
    if (start >= end)
        return true;
    uint64_t group = (uint64_t)block * channels;  // One block of every channel.
    uint64_t b0 = start / block, b1 = (end - 1) / block + 1;
    size_t bytes = (size_t)((b1 - b0) * group);
    if (!reserve(&d->raw, &d->raw_cap, bytes))
        return false;
    size_t got = 0;
    if (kfileSeek(d->f, (int64_t)(d->info.data_offset + b0 * group), SEEK_SET))
        got = kfileRead(d->f, d->raw, bytes);
    // Copies each channel's part of each block that was read whole.
    for (uint64_t b = b0; (b - b0 + 1) * group <= got; b++) {
        uint64_t lo = b * block > start ? b * block : start;
        uint64_t hi = (b + 1) * block < end ? (b + 1) * block : end;
        for (uint32_t c = 0; c < channels; c++) {
            const uint8_t *src = d->raw + (b - b0) * group + c * block + (lo - b * block);
            uint8_t *dst = d->chan + c * span + (size_t)((int64_t)lo - first);
            if (d->info.lsb_first) {
                for (uint64_t i = 0; i < hi - lo; i++)
                    dst[i] = g_reverse[src[i]];
            } else {
                memcpy(dst, src, (size_t)(hi - lo));
            }
        }
    }
    return true;
}

uint64_t dsdRead(DsdDecoder *d, float *out, uint64_t frames)
{
    uint32_t channels = d->info.channels;
    uint64_t done = 0;
    while (done < frames && d->frame < d->total_frames) {
        uint64_t n = frames - done;
        if (n > CHUNK_FRAMES)
            n = CHUNK_FRAMES;
        if (n > d->total_frames - d->frame)
            n = d->total_frames - d->frame;
        // PCM frame i filters the window of bytes that ends where frame i+1's step starts.
        int64_t first = (int64_t)((d->frame + 1) * d->step) - (int64_t)d->window;
        uint64_t end = (d->frame + n) * d->step;
        size_t span = (size_t)((int64_t)end - first);
        if (!loadBytes(d, first, end, span))
            break;
        for (uint64_t i = 0; i < n; i++) {
            for (uint32_t c = 0; c < channels; c++) {
                const uint8_t *src = d->chan + c * span + i * d->step;
                const float *t = d->table;
                // Four sums, so the additions don't wait on each other (the window is a multiple of 64).
                float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
                for (uint32_t k = 0; k < d->window; k += 4, t += 4 * 256) {
                    a0 += t[src[k]];
                    a1 += t[256 + src[k + 1]];
                    a2 += t[512 + src[k + 2]];
                    a3 += t[768 + src[k + 3]];
                }
                out[(done + i) * channels + c] = (a0 + a1) + (a2 + a3);
            }
        }
        done += n;
        d->frame += n;
    }
    return done;
}

bool dsdSeek(DsdDecoder *d, uint64_t frame)
{
    d->frame = frame < d->total_frames ? frame : d->total_frames;
    return true;
}
