// Linear-interpolation resampler for interleaved stereo float audio.
// audout only plays 48 kHz, so 44.1 kHz and other rates go through here.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RESAMPLER_CHUNK 1024

// Fills out with up to frames stereo frames. Returns frames written; 0 = end.
typedef uint64_t (*ResamplerSource)(void *ctx, float *out, uint64_t frames);

typedef struct {
    double step;  // Source frames per output frame.
    double pos;   // Read position in buf, in source frames.
    uint64_t count;
    bool eof;
    float buf[(RESAMPLER_CHUNK + 1) * 2];
} Resampler;

void resamplerReset(Resampler *r, uint32_t src_rate, uint32_t dst_rate);

// Writes up to frames output frames. Fewer means the source has ended.
uint64_t resamplerRun(Resampler *r, ResamplerSource src, void *ctx, float *out, uint64_t frames);
