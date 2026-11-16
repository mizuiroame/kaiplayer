// The Settings page: its rows, grouped in pages, and the sleep timer choices.
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
    SETTING_NOW_STYLE,
    SETTING_BACKGROUND,
    SETTING_SCREEN_OFF,
    SETTING_ABOUT,
    SETTING_OPEN_PLAYBACK,  // Rows that open a page.
    SETTING_OPEN_DISPLAY,
    SETTING_COUNT,
} SettingRow;

// Settings opens on the main page; Playback and Display hold the rest.
typedef enum {
    SETTINGS_PAGE_MAIN,
    SETTINGS_PAGE_PLAYBACK,
    SETTINGS_PAGE_DISPLAY,
    SETTINGS_PAGE_COUNT,
} SettingsPage;

#define SETTINGS_PAGE_MAX_ROWS 8

// The rows of a page, in order. Returns how many.
int settingsPageRows(SettingsPage page, SettingRow rows[SETTINGS_PAGE_MAX_ROWS]);
// The page a row opens, or SETTINGS_PAGE_MAIN if it doesn't open one.
SettingsPage settingsRowPage(SettingRow row);
// The row on the main page that opens page.
SettingRow settingsPageRow(SettingsPage page);
const char *settingsPageTitle(SettingsPage page);

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
