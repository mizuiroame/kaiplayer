// Now Playing's cassette style (see ui_nowstyle.h).
#pragma once

#include "ui_common.h"

// Draws the cassette in the size x size box of the cover. spin is how long
// the reels have turned (seconds); position and duration wind the tape.
void uiDrawCassette(int x, int y, int size, const UiTrack *track, double spin, double position, double duration);
