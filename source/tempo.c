#include "tempo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define COARSE_SHIFT 2   // Window places tried first: every second one,
#define COARSE_SAMPLE 4  // compared on every fourth frame;
#define FINE_SHIFT 2     // then the ones around the best, on every frame.
#define ENERGY_FLOOR 1e-9

static const int SPEEDS[TEMPO_SPEED_COUNT] = {750, TEMPO_PERMILLE, 1250, 1500, 1750, 2000};

static int clampSpeed(int permille)
{
    return permille < TEMPO_MIN ? TEMPO_MIN : permille > TEMPO_MAX ? TEMPO_MAX : permille;
}

void tempoReset(Tempo *t)
{
    t->input_count = 0;
    t->start = 0;
    t->next = 0.0;
    t->previous = -1;
    memset(t->tail, 0, sizeof(t->tail));
    t->out_count = t->out_at = 0;
    t->ended = t->flushed = false;
    t->position = 0;
}

void tempoInit(Tempo *t, int permille)
{
    t->permille = clampSpeed(permille);
    // A periodic Hann window: two of them half over each other add up to 1.
    for (int i = 0; i < TEMPO_WINDOW; i++)
        t->window[i] = 0.5f - 0.5f * (float)cos(2.0 * M_PI * i / TEMPO_WINDOW);
    tempoReset(t);
}

// Forgets the sound before frame `from`.
static void dropBefore(Tempo *t, int64_t from)
{
    if (from <= t->start)
        return;
    size_t n = (size_t)(from - t->start);
    if (n >= t->input_count) {
        t->input_count = 0;
    } else {
        memmove(t->input, t->input + n * 2, (t->input_count - n) * 2 * sizeof(float));
        t->input_count -= n;
    }
    t->start = from;
}

// Reads the sound up to frame `until` (not included), as far as it goes.
static void fillUntil(Tempo *t, TempoSource src, void *ctx, int64_t until)
{
    while (!t->ended && t->start + (int64_t)t->input_count < until && t->input_count < TEMPO_INPUT_MAX) {
        size_t want = (size_t)(until - t->start) - t->input_count;
        if (want > TEMPO_INPUT_MAX - t->input_count)
            want = TEMPO_INPUT_MAX - t->input_count;
        uint64_t got = src(ctx, t->input + t->input_count * 2, want);
        t->input_count += (size_t)got;
        if (got < want)
            t->ended = true;
    }
}

// Both channels of frame at (from the buffer's start); 0 past what was read.
static float monoAt(const Tempo *t, int64_t at)
{
    if (at < 0 || at >= (int64_t)t->input_count)
        return 0.0f;
    return t->input[at * 2] + t->input[at * 2 + 1];
}

// How well the window at cand continues the one before (at target), compared
// on every step-th frame of a hop: correlation over the candidate's energy.
static double similarity(const Tempo *t, int64_t cand, int64_t target, int step)
{
    double corr = 0.0, energy = ENERGY_FLOOR;
    for (int i = 0; i < TEMPO_HOP; i += step) {
        float c = monoAt(t, cand + i);
        corr += (double)c * monoAt(t, target + i);
        energy += (double)c * c;
    }
    return corr / sqrt(energy);
}

// The best place for the next window around ideal (frames from the buffer's start).
static int64_t bestPlace(const Tempo *t, int64_t ideal, int64_t target)
{
    int64_t lo = ideal - TEMPO_SEEK, hi = ideal + TEMPO_SEEK;
    int64_t last = (int64_t)t->input_count - TEMPO_HOP;  // A candidate's first hop must have been read.
    lo = lo < 0 ? 0 : lo;
    hi = hi > last ? last : hi;
    if (hi < lo)
        return ideal;
    int64_t best = lo;
    double best_score = -INFINITY;
    for (int64_t c = lo; c <= hi; c += COARSE_SHIFT) {
        double s = similarity(t, c, target, COARSE_SAMPLE);
        if (s > best_score) {
            best_score = s;
            best = c;
        }
    }
    int64_t around = best;
    best_score = -INFINITY;
    for (int64_t c = around - FINE_SHIFT; c <= around + FINE_SHIFT; c++) {
        if (c < lo || c > hi)
            continue;
        double s = similarity(t, c, target, 1);
        if (s > best_score) {
            best_score = s;
            best = c;
        }
    }
    return best;
}

static float sampleAt(const Tempo *t, int64_t at, int channel)
{
    return at >= 0 && at < (int64_t)t->input_count ? t->input[at * 2 + channel] : 0.0f;
}

// Makes the next hop of sound out. False once everything has been handed out.
static bool makeHop(Tempo *t, TempoSource src, void *ctx)
{
    if (t->flushed)
        return false;
    int64_t ideal = (int64_t)t->next;
    int64_t keep = t->previous < 0 ? ideal : ideal - TEMPO_SEEK;
    if (t->previous >= 0 && t->previous + TEMPO_HOP < keep)
        keep = t->previous + TEMPO_HOP;
    dropBefore(t, keep);
    fillUntil(t, src, ctx, ideal + TEMPO_SEEK + TEMPO_WINDOW);
    int64_t end = t->start + (int64_t)t->input_count;

    if (ideal >= end) {  // The sound has ended: the last window's second half, then nothing.
        t->flushed = true;
        if (t->previous < 0)
            return false;
        memcpy(t->out, t->tail, sizeof(t->tail));
        t->out_count = TEMPO_HOP;
        t->out_at = 0;
        t->position = ideal;
        return true;
    }

    int64_t chosen = ideal;
    if (t->previous >= 0)
        chosen = t->start + bestPlace(t, ideal - t->start, t->previous + TEMPO_HOP - t->start);
    int64_t at = chosen - t->start;
    for (int i = 0; i < TEMPO_HOP; i++) {
        for (int ch = 0; ch < 2; ch++) {
            float head = sampleAt(t, at + i, ch);
            // The first window after a reset starts at full level, not faded in.
            t->out[i * 2 + ch] = t->previous < 0 ? head : t->tail[i * 2 + ch] + head * t->window[i];
            t->tail[i * 2 + ch] = sampleAt(t, at + TEMPO_HOP + i, ch) * t->window[TEMPO_HOP + i];
        }
    }
    t->out_count = TEMPO_HOP;
    t->out_at = 0;
    t->position = ideal;  // Where the sound is meant to be: windows move a little around it.
    t->previous = chosen;
    t->next += (double)TEMPO_HOP * t->permille / TEMPO_PERMILLE;
    return true;
}

uint64_t tempoRun(Tempo *t, TempoSource src, void *ctx, float *out, uint64_t frames)
{
    uint64_t done = 0;
    while (done < frames) {
        if (t->out_at >= t->out_count && !makeHop(t, src, ctx))
            break;
        size_t n = t->out_count - t->out_at;
        if (n > frames - done)
            n = (size_t)(frames - done);
        memcpy(out + done * 2, t->out + t->out_at * 2, n * 2 * sizeof(float));
        t->out_at += n;
        done += n;
    }
    return done;
}

int64_t tempoPosition(const Tempo *t)
{
    // Through a hop, the sound heard moves from the window's start on, at the speed.
    if (t->out_count == 0)
        return t->position;
    return t->position + (int64_t)((double)t->out_at * t->permille / TEMPO_PERMILLE);
}

int tempoSpeedPermille(int index)
{
    return index >= 0 && index < TEMPO_SPEED_COUNT ? SPEEDS[index] : TEMPO_PERMILLE;
}

int tempoSpeedIndex(int permille)
{
    int best = TEMPO_SPEED_NORMAL;
    for (int i = 0; i < TEMPO_SPEED_COUNT; i++) {
        int d = SPEEDS[i] - permille, b = SPEEDS[best] - permille;
        if ((d < 0 ? -d : d) < (b < 0 ? -b : b))
            best = i;
    }
    return best;
}

int tempoSpeedStep(int index, int delta)
{
    int next = index + delta;
    return next < 0 ? 0 : next >= TEMPO_SPEED_COUNT ? TEMPO_SPEED_COUNT - 1 : next;
}

void tempoFormatSpeed(char *out, size_t size, int permille)
{
    int whole = permille / TEMPO_PERMILLE, part = permille % TEMPO_PERMILLE;
    if (part == 0)
        snprintf(out, size, "%dx", whole);
    else if (part % 100 == 0)
        snprintf(out, size, "%d.%dx", whole, part / 100);
    else
        snprintf(out, size, "%d.%02dx", whole, part / 10);
}
