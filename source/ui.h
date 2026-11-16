// Draws one frame of the interface.
#pragma once

#include <stdint.h>

#include "app.h"
#include "player.h"

#define UI_LIST_ROWS 10

void uiDraw(const App *app, const PlayerStatus *status, uint64_t now_ns);
// What the screen shows while it is off (a black screen with a hint).
void uiDrawScreenOff(void);
