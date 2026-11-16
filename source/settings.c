#include "settings.h"

static const char *const ROW_LABELS[SETTING_COUNT] = {
    "Music folder", "Sleep timer", "Resume where you left off", "Clock", "Equalizer", "ReplayGain (even loudness)",
    "Accent color", "Screen off while playing", "About",
};

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
    return row != SETTING_ABOUT && row != SETTING_LIBRARY;  // Those open with A.
}
