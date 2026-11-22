// AAC, ALAC and Opus files (.m4a, .mp4, .aac, .opus, and Opus in .ogg)
// with FFmpeg (LGPL 2.1), read through kfile like every other music file:
// the decoder for decoder.c, and the tags and the embedded cover for tags_file.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audioformat.h"
#include "kfile.h"
#include "tags.h"

typedef struct FfAudio FfAudio;

// True for the extensions FFmpeg reads: .m4a, .mp4, .aac, .opus (not .ogg,
// which is Vorbis unless ffaudioOggIsOpus says otherwise).
bool ffaudioIsFile(const char *path);

// Opens the file's first audio stream for playing. f stays the caller's,
// and open until ffaudioClose. NULL if FFmpeg can't play it.
FfAudio *ffaudioOpen(KFile *f);
void ffaudioClose(FfAudio *a);

// 1 for a mono file, otherwise 2: more channels are mixed down to stereo.
uint32_t ffaudioChannels(const FfAudio *a);
uint32_t ffaudioSampleRate(const FfAudio *a);
// Length in frames, from the stream's duration; 0 if unknown.
uint64_t ffaudioTotalFrames(const FfAudio *a);
// Reads up to frames frames, interleaved float in ffaudioChannels. 0 at the end.
uint64_t ffaudioRead(FfAudio *a, float *out, uint64_t frames);
// Lands on that frame exactly (decoding from a little before it).
bool ffaudioSeek(FfAudio *a, uint64_t frame);
// "ALAC", 24 bits, 96000 Hz; "AAC", 256 kbps; "Opus".
void ffaudioFormat(const FfAudio *a, AudioFormat *out);

// The tags and the attached picture (MP4 covr, Opus METADATA_BLOCK_PICTURE,
// ID3 in .aac), into t as the other readers fill it. False if the file
// couldn't be read as one FFmpeg knows.
bool ffaudioReadTags(KFile *f, TrackTags *t);

// True if an Ogg file holds Opus (FFmpeg plays it), false for Vorbis and
// anything else (stb_vorbis, as before). Reads its first page and goes back
// to the start.
bool ffaudioOggIsOpus(KFile *f);
