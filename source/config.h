// Settings kept between runs in sdmc:/config/kaiplayer/settings.txt.
#pragma once

#include <stdbool.h>

#include "queue.h"
#include "util.h"

typedef struct {
    char folder[UTIL_PATH_MAX];  // Last folder browsed.
    bool shuffle;
    RepeatMode repeat;
    bool resume;                 // Reopen the last queue at startup.
    bool clock_24h;              // 21:47 rather than 9:47 PM.
} Config;

// Fills defaults, then overrides them with the saved file if it exists.
void configLoad(Config *c);
bool configSave(const Config *c);
