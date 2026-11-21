#include "eq.h"

#include <math.h>
#include <string.h>

#include "lang.h"

#define BAND_Q 1.41  // About one octave wide, like the band spacing.
#define PI 3.14159265358979323846

static const double FREQUENCIES[EQ_BANDS] = {31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
static const char *const LABELS[EQ_BANDS] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
static const char *const PRESET_NAMES[EQ_PRESET_COUNT] = {
    N_("Flat"), N_("Bass"), N_("Treble"), N_("Rock"),   N_("Pop"),
    N_("Jazz"), N_("Classical"), N_("Vocal"), N_("Custom"),
};
static const int PRESET_GAINS[EQ_PRESET_CUSTOM][EQ_BANDS] = {
    [EQ_PRESET_FLAT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    [EQ_PRESET_BASS] = {6, 5, 4, 2, 0, 0, 0, 0, 0, 0},
    [EQ_PRESET_TREBLE] = {0, 0, 0, 0, 0, 0, 2, 4, 5, 6},
    [EQ_PRESET_ROCK] = {4, 3, 2, 0, -1, 0, 2, 3, 4, 4},
    [EQ_PRESET_POP] = {-1, 0, 2, 3, 4, 3, 1, 0, 0, -1},
    [EQ_PRESET_JAZZ] = {3, 2, 1, 2, -1, -1, 0, 1, 2, 3},
    [EQ_PRESET_CLASSICAL] = {4, 3, 2, 1, 0, 0, 0, 1, 2, 3},
    [EQ_PRESET_VOCAL] = {-2, -2, -1, 1, 3, 4, 3, 1, 0, -1},
};

double eqBandFrequency(int band)
{
    return band >= 0 && band < EQ_BANDS ? FREQUENCIES[band] : 0.0;
}

const char *eqBandLabel(int band)
{
    return band >= 0 && band < EQ_BANDS ? LABELS[band] : "";
}

const char *eqPresetName(EqPreset preset)
{
    return preset >= 0 && preset < EQ_PRESET_COUNT ? T(PRESET_NAMES[preset]) : "";
}

bool eqPresetGains(EqPreset preset, int gains[EQ_BANDS])
{
    if (preset < 0 || preset >= EQ_PRESET_CUSTOM)
        return false;
    memcpy(gains, PRESET_GAINS[preset], sizeof(PRESET_GAINS[preset]));
    return true;
}

// RBJ Audio EQ Cookbook peaking filter.
static EqBiquad peaking(double frequency, double gain_db, double sample_rate)
{
    double a = pow(10.0, gain_db / 40.0);
    double w0 = 2.0 * PI * frequency / sample_rate;
    double alpha = sin(w0) / (2.0 * BAND_Q);
    double cosw = cos(w0);
    double a0 = 1.0 + alpha / a;
    return (EqBiquad){
        .b0 = (1.0 + alpha * a) / a0,
        .b1 = (-2.0 * cosw) / a0,
        .b2 = (1.0 - alpha * a) / a0,
        .a1 = (-2.0 * cosw) / a0,
        .a2 = (1.0 - alpha / a) / a0,
    };
}

// Gain of the whole cascade at one frequency (linear).
static double cascadeGain(const Eq *eq, double frequency, double sample_rate)
{
    double w = 2.0 * M_PI * frequency / sample_rate, gain = 1.0;
    double c1 = cos(w), s1 = sin(w), c2 = cos(2.0 * w), s2 = sin(2.0 * w);
    for (int b = 0; b < EQ_BANDS; b++) {
        const EqBiquad *q = &eq->bands[b];
        double nr = q->b0 + q->b1 * c1 + q->b2 * c2, ni = -(q->b1 * s1 + q->b2 * s2);
        double dr = 1.0 + q->a1 * c1 + q->a2 * c2, di = -(q->a1 * s1 + q->a2 * s2);
        gain *= sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    return gain;
}

#define RESPONSE_POINTS 120

void eqConfigure(Eq *eq, const int gains[EQ_BANDS], double sample_rate)
{
    bool was_active = eq->active;
    eq->active = false;
    for (int b = 0; b < EQ_BANDS; b++) {
        int g = gains[b] < EQ_GAIN_MIN ? EQ_GAIN_MIN : gains[b] > EQ_GAIN_MAX ? EQ_GAIN_MAX : gains[b];
        // A band above Nyquist (16 kHz at a low rate) is left flat.
        bool usable = FREQUENCIES[b] < sample_rate * 0.45;
        eq->bands[b] = peaking(FREQUENCIES[b], usable ? g : 0, sample_rate);
        eq->active |= usable && g != 0;
    }
    // Neighbouring bands overlap, so the loudest point can be above any
    // single gain: find it on the real response and take it off up front.
    double peak = 1.0;
    for (int i = 0; i < RESPONSE_POINTS; i++) {
        double f = 20.0 * pow(1000.0, (double)i / (RESPONSE_POINTS - 1));  // 20 Hz to 20 kHz.
        if (f < sample_rate * 0.49) {
            double g = cascadeGain(eq, f, sample_rate);
            peak = g > peak ? g : peak;
        }
    }
    eq->preamp = (float)(1.0 / peak);
    if (eq->active && !was_active)
        eqReset(eq);  // Old filter state would click.
}

void eqReset(Eq *eq)
{
    memset(eq->z1, 0, sizeof(eq->z1));
    memset(eq->z2, 0, sizeof(eq->z2));
}

void eqProcess(Eq *eq, float *stereo, uint64_t frames)
{
    if (!eq->active)
        return;
    for (uint64_t i = 0; i < frames; i++) {
        for (int c = 0; c < 2; c++) {
            double x = stereo[i * 2 + c] * eq->preamp;
            for (int b = 0; b < EQ_BANDS; b++) {
                const EqBiquad *q = &eq->bands[b];
                double y = q->b0 * x + eq->z1[b][c];
                eq->z1[b][c] = q->b1 * x - q->a1 * y + eq->z2[b][c];
                eq->z2[b][c] = q->b2 * x - q->a2 * y;
                x = y;
            }
            stereo[i * 2 + c] = (float)x;
        }
    }
}
