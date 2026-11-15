// Settings kept between runs in sdmc:/config/kaiplayer/settings.txt.
#pragma once

#include <stdbool.h>

#include "queue.h"
#include "util.h"

typedef enum {
    RESUME_OFF,
    RESUME_PAUSED,   // Reopen the last queue, paused.
    RESUME_PLAYING,  // Reopen it and keep playing.
    RESUME_MODE_COUNT,
} ResumeMode;

typedef struct {
    char folder[UTIL_PATH_MAX];  // Last folder browsed.
    bool shuffle;
    RepeatMode repeat;
    ResumeMode resume;           // What to do with the last queue at startup.
    bool clock_24h;              // 21:47 rather than 9:47 PM.
} Config;

// Fills defaults, then overrides them with the saved file if it exists.
void configLoad(Config *c);
bool configSave(const Config *c);
