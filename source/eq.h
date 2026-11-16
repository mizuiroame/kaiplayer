// A 10-band graphic equalizer (peaking biquads, RBJ cookbook) for
// interleaved stereo float audio. Pure DSP: the player runs it on its thread.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EQ_BANDS 10
#define EQ_GAIN_MIN (-12)
#define EQ_GAIN_MAX 12

typedef enum {
    EQ_PRESET_FLAT,
    EQ_PRESET_BASS,
    EQ_PRESET_TREBLE,
    EQ_PRESET_ROCK,
    EQ_PRESET_POP,
    EQ_PRESET_JAZZ,
    EQ_PRESET_CLASSICAL,
    EQ_PRESET_VOCAL,
    EQ_PRESET_CUSTOM,
    EQ_PRESET_COUNT,
} EqPreset;

typedef struct {
    double b0, b1, b2, a1, a2;  // Normalized by a0.
} EqBiquad;

typedef struct {
    EqBiquad bands[EQ_BANDS];
    double z1[EQ_BANDS][2], z2[EQ_BANDS][2];  // Transposed direct form II state.
    float preamp;   // Keeps boosted bands from clipping.
    bool active;    // False when every band is 0 dB: audio passes untouched.
} Eq;

// The center frequency of a band, in Hz, and a label such as "1k".
double eqBandFrequency(int band);
const char *eqBandLabel(int band);

const char *eqPresetName(EqPreset preset);
// The gains of a preset. Custom has no fixed gains: it returns false.
bool eqPresetGains(EqPreset preset, int gains[EQ_BANDS]);

// Sets the gains (dB, clamped to the range) for a sample rate. The filter
// state is kept, so a change while playing doesn't click.
void eqConfigure(Eq *eq, const int gains[EQ_BANDS], double sample_rate);
void eqReset(Eq *eq);  // Clears the state, for a new track.
void eqProcess(Eq *eq, float *stereo, uint64_t frames);
