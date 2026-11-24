// Settings kept between runs in settings.txt in KaiPlayer's folder (next to
// its NRO, see approot.h; sdmc:/config/kaiplayer before).
#pragma once

#include <stdbool.h>

#include "background.h"
#include "eq.h"
#include "queue.h"
#include "util.h"

typedef enum {
    RESUME_OFF,
    RESUME_PAUSED,   // Reopen the last queue, paused.
    RESUME_PLAYING,  // Reopen it and keep playing.
    RESUME_MODE_COUNT,
} ResumeMode;

// Where the library comes from.
typedef enum {
    LIBRARY_CARD,     // The music folder on the card.
    LIBRARY_NETWORK,  // The network folder (dav.h).
    LIBRARY_SERVER,   // The Navidrome server (subsonic.h).
    LIBRARY_SOURCE_COUNT,
} LibrarySource;

typedef struct {
    char folder[UTIL_PATH_MAX];  // Last folder browsed.
    char library[UTIL_PATH_MAX]; // The music folder; empty until chosen.
    LibrarySource source;        // Where the library shown comes from.
    bool playlists_moved;        // The old sdmc:/playlists were copied into it.
    bool welcomed;               // The first run's welcome was answered ("Later" or a source set).
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
    int now_style;               // NowStyle: cover, vinyl, CD or cassette.
    bool show_lyrics;            // Now Playing shows a song's synced lyrics when it has them.
    char background[BACKGROUND_NAME_MAX];  // An image in <library>/Backgrounds; empty for the cover colors.
    int background_blur;         // BackgroundBlur.
    char language[64];           // Its id: "en", "es", or a file in config/lang; "" is English.
    char language_name[64];      // Its name, as Settings shows it (not kept: set at start).
    bool update_check;           // Look for a new version once a day (update.h).
    long long update_checked;    // When it last looked, in seconds since 1970; 0 never.
    char update_tag[32];         // The newer version it found then ("v1.4.0"), "" for none.
} Config;

// Fills defaults, then overrides them with the saved file if it exists.
// KaiPlayer's folder, for its settings, history, catalog and resume files:
// set once at start (main), sdmc:/config/kaiplayer until then.
void configSetFolder(const char *folder);
const char *configFolder(void);

void configLoad(Config *c);
bool configSave(const Config *c);
