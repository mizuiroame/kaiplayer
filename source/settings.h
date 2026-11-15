// The Settings page: its rows and the sleep timer choices.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SETTING_SLEEP,
    SETTING_RESUME,
    SETTING_CLOCK,
    SETTING_ABOUT,
    SETTING_COUNT,
} SettingRow;

#define SLEEP_OPTION_COUNT 6

// Minutes for a sleep timer option; option 0 is off.
int settingsSleepMinutes(int option);
// Steps an option by delta, wrapping around.
int settingsSleepStep(int option, int delta);

// Label and current value of a row, for drawing.
const char *settingsRowLabel(SettingRow row);
bool settingsRowEditable(SettingRow row);
