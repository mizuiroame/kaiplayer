// Settings kept between runs in sdmc:/config/kaiplayer/settings.txt.
#pragma once

#include <stdbool.h>

#include "eq.h"
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
    char library[UTIL_PATH_MAX]; // The music folder; empty until chosen.
    bool playlists_moved;        // The old sdmc:/playlists were copied into it.
    bool shuffle;
    RepeatMode repeat;
    ResumeMode resume;           // What to do with the last queue at startup.
    bool clock_24h;              // 21:47 rather than 9:47 PM.
    bool eq_on;
    int eq_preset;               // EqPreset; Custom uses eq_gains.
    int eq_gains[EQ_BANDS];      // dB per band.
    int replaygain;              // ReplayGainMode.
    int accent;                  // Theme accent, see theme.h.
    int screen_off_minutes;      // Turn the screen off after this long idle while playing; 0 never.
} Config;

// Fills defaults, then overrides them with the saved file if it exists.
void configLoad(Config *c);
bool configSave(const Config *c);
