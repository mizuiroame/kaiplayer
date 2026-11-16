// Now Playing's record, CD and cassette (nowstyle.h lists the styles).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui_common.h"

// Draws the style in the size x size box of the cover. False for the plain
// cover, which the caller draws. position and duration (seconds) wind the
// cassette's tape.
bool uiDrawNowStyle(int style, int x, int y, int size, const UiTrack *track, bool playing, uint64_t now_ns,
                    double position, double duration);

// Seconds the discs and reels have turned: time passes only while playing.
double uiNowStyleSpin(bool playing, uint64_t now_ns);
