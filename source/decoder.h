// One interface over the bundled decoders: MP3, FLAC, WAV (dr_libs),
// Ogg Vorbis (stb_vorbis) and DSF/DFF (dsd.c, as PCM). Output is always
// interleaved stereo float.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Decoder Decoder;

// Opens a file by its extension. NULL if the file cannot be decoded.
Decoder *decoderOpen(const char *path);
void decoderClose(Decoder *d);

uint32_t decoderSampleRate(const Decoder *d);
// Length in frames, or 0 if unknown.
uint64_t decoderTotalFrames(const Decoder *d);

// Reads up to frames stereo frames. Returns frames read; 0 at the end.
uint64_t decoderReadStereo(Decoder *d, float *out, uint64_t frames);
bool decoderSeek(Decoder *d, uint64_t frame);
