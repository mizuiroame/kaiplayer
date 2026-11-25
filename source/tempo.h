// Another reading speed for audiobooks, with the voice's pitch kept: WSOLA
// (waveform-similarity overlap-add) on interleaved stereo float, between the
// resampler and the equalizer. The windows are laid half over each other at
// a steady pace on the way out, and taken from the sound at speed times that
// pace, each nudged to where it best continues the one before it, so voices
// don't flutter. Pure: no console calls, for the host tests.
//
// The speeds are KaiVideo's list (playback.c), from 0.75x up to 2x.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEMPO_PERMILLE 1000          // Speeds are in thousandths: 1250 is 1.25x.
#define TEMPO_SPEED_COUNT 6
#define TEMPO_SPEED_NORMAL 1         // The index of 1x.
#define TEMPO_MIN 500
#define TEMPO_MAX 2000

#define TEMPO_WINDOW 1536            // Frames: 32 ms at 48 kHz, longer than a low voice's period.
#define TEMPO_HOP (TEMPO_WINDOW / 2) // Frames out per window.
#define TEMPO_SEEK 480               // Frames either way a window may move: 10 ms at 48 kHz.
#define TEMPO_INPUT_MAX (TEMPO_WINDOW * 2 + TEMPO_SEEK * 2 + TEMPO_HOP * 2 + TEMPO_HOP)

// Reads up to frames stereo frames into out; fewer only at the end.
typedef uint64_t (*TempoSource)(void *ctx, float *out, uint64_t frames);

typedef struct {
    int permille;
    float window[TEMPO_WINDOW];
    float input[TEMPO_INPUT_MAX * 2];  // Stereo frames from `start` on.
    size_t input_count;
    int64_t start;                     // The frame of the sound input[0] is.
    double next;                       // Where the next window should come from, ideally.
    int64_t previous;                  // Where the last window came from; -1 before the first.
    float tail[TEMPO_HOP * 2];         // The last window's second half, waiting to be added.
    float out[TEMPO_HOP * 2];          // A hop made but not handed out yet.
    size_t out_count, out_at;
    bool ended;                        // The source gave its last frame.
    bool flushed;                      // And the tail was handed out.
    int64_t position;                  // The frame of the sound heard at the end of what was handed out.
} Tempo;

// Starts over at the sound's frame 0 (after a seek or a new song), keeping the speed.
void tempoReset(Tempo *t);
// Sets the speed (clamped to TEMPO_MIN..TEMPO_MAX) and starts over.
void tempoInit(Tempo *t, int permille);

// Up to frames frames at the speed into out, read from src as needed.
// Returns how many; fewer than asked only at the source's end.
uint64_t tempoRun(Tempo *t, TempoSource src, void *ctx, float *out, uint64_t frames);

// How far into the sound (in its frames, from the reset) what was handed out has got.
int64_t tempoPosition(const Tempo *t);

// The speed at index in the list (normal speed if there's no such index).
int tempoSpeedPermille(int index);
// The index of the list's speed nearest permille.
int tempoSpeedIndex(int permille);
// The index delta steps from index, kept within the list.
int tempoSpeedStep(int index, int delta);
// "1x", "1.25x", "0.75x".
void tempoFormatSpeed(char *out, size_t size, int permille);
