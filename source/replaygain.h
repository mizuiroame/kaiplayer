// Which ReplayGain value a song plays with.
#pragma once

#include <stdbool.h>

#include "tags.h"

typedef enum {
    REPLAYGAIN_OFF,
    REPLAYGAIN_TRACK,  // Each song at the same loudness.
    REPLAYGAIN_ALBUM,  // Albums at the same loudness, keeping their own dynamics.
    REPLAYGAIN_AUTO,   // Album gain for an album in order; track gain for playlists and shuffle.
    REPLAYGAIN_MODE_COUNT,
} ReplayGainMode;

// The gain in dB for a song's tags. mixed_albums: the queue mixes albums
// (a playlist). Album falls back to track gain; no tag means 0 dB.
float replayGainDb(ReplayGainMode mode, bool shuffle, bool mixed_albums, const TrackTags *tags);
// dB to a linear factor. Without peak data a boost could clip, so positive
// gains play at 1.0.
float replayGainFactor(float db);
