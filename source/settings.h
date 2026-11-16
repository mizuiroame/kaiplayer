// The Settings page: its rows and the sleep timer choices.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SETTING_LIBRARY,
    SETTING_SLEEP,
    SETTING_RESUME,
    SETTING_CLOCK,
    SETTING_EQUALIZER,
    SETTING_REPLAYGAIN,
    SETTING_ACCENT,
    SETTING_SCREEN_OFF,
    SETTING_ABOUT,
    SETTING_COUNT,
} SettingRow;

#define SLEEP_STEP_MINUTES 5
#define SLEEP_MAX_MINUTES 600  // 10 hours.

// Moves the sleep timer by delta steps of 5 minutes, to the next multiple
// of 5, within 0 (off) and SLEEP_MAX_MINUTES.
int settingsSleepStep(int minutes, int delta);
// Clamps typed minutes to 0..SLEEP_MAX_MINUTES.
int settingsSleepClamp(long minutes);

// The "turn the screen off after" choices, in minutes; 0 is never.
int settingsScreenOffStep(int minutes, int delta);

// Label and current value of a row, for drawing.
const char *settingsRowLabel(SettingRow row);
bool settingsRowEditable(SettingRow row);
