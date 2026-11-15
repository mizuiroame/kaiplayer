// Draws one frame of the interface.
#pragma once

#include "app.h"
#include "player.h"

#define UI_LIST_ROWS 13

void uiDraw(const App *app, const PlayerStatus *status, uint64_t now_ns);
