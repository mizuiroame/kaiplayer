#include "resampler.h"

#include <string.h>

void resamplerReset(Resampler *r, uint32_t src_rate, uint32_t dst_rate)
{
    r->step = (src_rate && dst_rate) ? (double)src_rate / (double)dst_rate : 1.0;
    r->pos = 0.0;
    r->count = 0;
    r->eof = false;
}

// Drops consumed frames and reads more. False once the source is exhausted.
static bool refill(Resampler *r, ResamplerSource src, void *ctx)
{
    if (r->eof)
        return false;

    uint64_t keep_from = (uint64_t)r->pos;
    if (keep_from > r->count)
        keep_from = r->count;
    uint64_t kept = r->count - keep_from;
    memmove(r->buf, r->buf + keep_from * 2, kept * 2 * sizeof(float));
    r->count = kept;
    r->pos -= (double)keep_from;

    uint64_t space = (RESAMPLER_CHUNK + 1) - r->count;
    uint64_t got = src(ctx, r->buf + r->count * 2, space);
    if (got == 0) {
        r->eof = true;
        return false;
    }
    r->count += got;
    return true;
}

uint64_t resamplerRun(Resampler *r, ResamplerSource src, void *ctx, float *out, uint64_t frames)
{
    uint64_t written = 0;
    while (written < frames) {
        while ((uint64_t)r->pos + 1 >= r->count) {
            if (!refill(r, src, ctx))
                return written;
        }
        uint64_t i = (uint64_t)r->pos;
        float frac = (float)(r->pos - (double)i);
        const float *a = r->buf + i * 2;
        const float *b = a + 2;
        out[written * 2] = a[0] + (b[0] - a[0]) * frac;
        out[written * 2 + 1] = a[1] + (b[1] - a[1]) * frac;
        written++;
        r->pos += r->step;
    }
    return written;
}
