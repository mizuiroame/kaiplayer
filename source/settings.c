#include "settings.h"

static const int SLEEP_MINUTES[SLEEP_OPTION_COUNT] = {0, 15, 30, 45, 60, 90};
static const char *const ROW_LABELS[SETTING_COUNT] = {
    "Sleep timer", "Resume where you left off", "Clock", "About",
};

int settingsSleepMinutes(int option)
{
    return option >= 0 && option < SLEEP_OPTION_COUNT ? SLEEP_MINUTES[option] : 0;
}

int settingsSleepStep(int option, int delta)
{
    return ((option + delta) % SLEEP_OPTION_COUNT + SLEEP_OPTION_COUNT) % SLEEP_OPTION_COUNT;
}

const char *settingsRowLabel(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT ? ROW_LABELS[row] : "";
}

bool settingsRowEditable(SettingRow row)
{
    return row != SETTING_ABOUT;
}
