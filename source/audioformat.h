// A playing file's format, and its label for the UI.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// What a file holds, for showing: "FLAC", 24 bits, 96000 Hz; "AAC", 256 kbps.
typedef struct {
    char codec[16];        // "MP3", "FLAC", "OGG", "WAV", "DSD64", "AAC", "ALAC", "Opus"…; empty when nothing plays.
    uint32_t sample_rate;  // The file's own rate (DSD's 1-bit rate for DSD), not the console's.
    unsigned bits;         // Per sample for PCM and ALAC; 0 for lossy formats and DSD.
    unsigned kbps;         // AAC's and Opus's bit rate; 0 when not shown.
} AudioFormat;

// "FLAC · 24-bit · 96 kHz", "MP3 · 44.1 kHz", "DSD64 · 2.8 MHz", "AAC · 256 kbps".
// Empty for no format.
void decoderFormatLabel(const AudioFormat *f, char *out, size_t size);
