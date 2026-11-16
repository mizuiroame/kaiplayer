// DSF and DSDIFF (.dff) files, played as PCM. The 1-bit DSD stream goes
// through a low-pass filter and is decimated to 88.2 kHz (96 kHz for DSD
// at multiples of 48 kHz); the player's resampler takes it from there.
// DSD64 to DSD512, uncompressed only (not DST).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define DSD_MAX_CHANNELS 6

typedef struct {
    uint32_t channels;
    uint32_t dsd_rate;           // 1-bit samples per second per channel: 2822400 for DSD64.
    uint64_t data_offset;        // Where the audio starts in the file.
    uint64_t bytes_per_channel;  // Audio bytes per channel (8 samples each).
    uint32_t block;              // Bytes of one channel before the next: 4096 in DSF, 1 in DSDIFF.
    bool lsb_first;              // DSF stores the earliest sample in the lowest bit.
    uint64_t id3_offset;         // Where an ID3v2 tag starts, or 0.
} DsdInfo;

// Reads a DSF or DSDIFF file's headers. False if it isn't one KaiPlayer can play.
bool dsdReadInfo(FILE *f, DsdInfo *info);

typedef struct DsdDecoder DsdDecoder;

DsdDecoder *dsdOpen(const char *path);
void dsdClose(DsdDecoder *d);
uint32_t dsdChannels(const DsdDecoder *d);
uint32_t dsdSampleRate(const DsdDecoder *d);  // Of the PCM out.
uint32_t dsdRate(const DsdDecoder *d);        // Of the file's 1-bit stream: 2822400 for DSD64.
uint64_t dsdTotalFrames(const DsdDecoder *d);
// Reads up to frames PCM frames, interleaved. Returns frames read; 0 at the end.
uint64_t dsdRead(DsdDecoder *d, float *out, uint64_t frames);
bool dsdSeek(DsdDecoder *d, uint64_t frame);
