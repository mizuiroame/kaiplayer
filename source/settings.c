#include "settings.h"

static const char *const ROW_LABELS[SETTING_COUNT] = {
    "Music folder", "Sleep timer", "Resume where you left off", "Clock", "Equalizer", "ReplayGain (even loudness)",
    "Accent color", "Now Playing style", "Background", "Screen off while playing", "About", "Playback", "Display",
};

static const SettingRow MAIN_ROWS[] = {SETTING_LIBRARY, SETTING_OPEN_PLAYBACK, SETTING_OPEN_DISPLAY, SETTING_ABOUT};
static const SettingRow PLAYBACK_ROWS[] = {SETTING_SLEEP, SETTING_RESUME, SETTING_EQUALIZER, SETTING_REPLAYGAIN};
static const SettingRow DISPLAY_ROWS[] = {SETTING_ACCENT, SETTING_NOW_STYLE, SETTING_BACKGROUND, SETTING_CLOCK,
                                          SETTING_SCREEN_OFF};

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

int settingsPageRows(SettingsPage page, SettingRow rows[SETTINGS_PAGE_MAX_ROWS])
{
    const SettingRow *from = page == SETTINGS_PAGE_PLAYBACK ? PLAYBACK_ROWS
                           : page == SETTINGS_PAGE_DISPLAY  ? DISPLAY_ROWS
                                                            : MAIN_ROWS;
    int count = page == SETTINGS_PAGE_PLAYBACK ? COUNT_OF(PLAYBACK_ROWS)
              : page == SETTINGS_PAGE_DISPLAY  ? COUNT_OF(DISPLAY_ROWS)
                                               : COUNT_OF(MAIN_ROWS);
    for (int i = 0; i < count; i++)
        rows[i] = from[i];
    return count;
}

SettingsPage settingsRowPage(SettingRow row)
{
    return row == SETTING_OPEN_PLAYBACK ? SETTINGS_PAGE_PLAYBACK
         : row == SETTING_OPEN_DISPLAY  ? SETTINGS_PAGE_DISPLAY
                                        : SETTINGS_PAGE_MAIN;
}

SettingRow settingsPageRow(SettingsPage page)
{
    return page == SETTINGS_PAGE_PLAYBACK ? SETTING_OPEN_PLAYBACK
         : page == SETTINGS_PAGE_DISPLAY  ? SETTING_OPEN_DISPLAY
                                          : SETTING_LIBRARY;
}

const char *settingsPageTitle(SettingsPage page)
{
    return page == SETTINGS_PAGE_PLAYBACK ? "Playback" : page == SETTINGS_PAGE_DISPLAY ? "Display" : "Settings";
}

int settingsSleepClamp(long minutes)
{
    return minutes < 0 ? 0 : minutes > SLEEP_MAX_MINUTES ? SLEEP_MAX_MINUTES : (int)minutes;
}

int settingsSleepStep(int minutes, int delta)
{
    minutes = settingsSleepClamp(minutes);
    int step = minutes / SLEEP_STEP_MINUTES;
    if (delta > 0)
        step += delta;
    else if (delta < 0)
        step += minutes % SLEEP_STEP_MINUTES ? delta + 1 : delta;  // 7 goes down to 5.
    return settingsSleepClamp((long)step * SLEEP_STEP_MINUTES);
}

static const int SCREEN_OFF_CHOICES[] = {0, 1, 2, 5, 10, 30};
#define SCREEN_OFF_COUNT ((int)(sizeof(SCREEN_OFF_CHOICES) / sizeof(SCREEN_OFF_CHOICES[0])))

int settingsScreenOffStep(int minutes, int delta)
{
    int index = 0;
    for (int i = 0; i < SCREEN_OFF_COUNT; i++) {
        if (SCREEN_OFF_CHOICES[i] <= minutes)
            index = i;
    }
    index = ((index + delta) % SCREEN_OFF_COUNT + SCREEN_OFF_COUNT) % SCREEN_OFF_COUNT;
    return SCREEN_OFF_CHOICES[index];
}

const char *settingsRowLabel(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT ? ROW_LABELS[row] : "";
}

bool settingsRowEditable(SettingRow row)
{
    // Those open with A: About, the folder picker and the pages.
    return row != SETTING_ABOUT && row != SETTING_LIBRARY && settingsRowPage(row) == SETTINGS_PAGE_MAIN;
}
