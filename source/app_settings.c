// The Settings page and the sleep timer.
#include <stdio.h>
#include <stdlib.h>

#include "app.h"
#include "app_buttons.h"
#include "keyboard_system.h"
#include "player.h"
#include "resume.h"

#define SLEEP_FADE_SECONDS 10.0
#define NS_PER_MINUTE 60000000000ull

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

void appOpenSettings(App *app)
{
    app->settings_open = true;
    app->settings_cursor = SETTING_SLEEP;
}

static void setSleep(App *app, int minutes)
{
    app->sleep_minutes = minutes;
    app->sleep_deadline_ns = minutes ? nowNs() + (uint64_t)minutes * NS_PER_MINUTE : 0;
}

// Types the sleep timer's minutes on the number pad; 0 turns it off.
static void typeSleepMinutes(App *app)
{
    char initial[8] = "", typed[16];
    if (app->sleep_minutes)
        snprintf(initial, sizeof(initial), "%d", app->sleep_minutes);
    SystemKeyboardResult result =
        systemKeyboardNumber("Sleep timer, in minutes (0 turns it off)", initial, typed, sizeof(typed), 3);
    if (result == SYSTEM_KEYBOARD_OK)
        setSleep(app, settingsSleepClamp(strtol(typed, NULL, 10)));
    else if (result == SYSTEM_KEYBOARD_UNAVAILABLE)
        appChangeSetting(app, SETTING_SLEEP, 1);  // No number pad: step instead.
}

void appActivateSetting(App *app, SettingRow row)
{
    if (row == SETTING_SLEEP)
        typeSleepMinutes(app);
    else
        appChangeSetting(app, row, 1);
}

void appChangeSetting(App *app, SettingRow row, int delta)
{
    switch (row) {
    case SETTING_SLEEP:
        setSleep(app, settingsSleepStep(app->sleep_minutes, delta));
        break;
    case SETTING_RESUME:
        app->config.resume = (ResumeMode)((app->config.resume + delta + RESUME_MODE_COUNT) % RESUME_MODE_COUNT);
        if (app->config.resume == RESUME_OFF)
            resumeClear();  // Off means KaiPlayer forgets the last queue.
        configSave(&app->config);
        break;
    case SETTING_CLOCK:
        app->config.clock_24h = !app->config.clock_24h;
        configSave(&app->config);
        break;
    default:
        break;
    }
}

void appHandleSettings(App *app, uint64_t pressed)
{
    int cursor = (int)app->settings_cursor;
    if (pressed & BUTTONS_UP)
        cursor = cursor > 0 ? cursor - 1 : 0;
    if (pressed & BUTTONS_DOWN)
        cursor = cursor + 1 < SETTING_COUNT ? cursor + 1 : SETTING_COUNT - 1;
    app->settings_cursor = (SettingRow)cursor;
    if (pressed & HidNpadButton_A)
        appActivateSetting(app, app->settings_cursor);
    if (pressed & BUTTONS_RIGHT)
        appChangeSetting(app, app->settings_cursor, 1);
    if (pressed & BUTTONS_LEFT)
        appChangeSetting(app, app->settings_cursor, -1);
    if (pressed & (HidNpadButton_B | HidNpadButton_Plus))
        app->settings_open = false;
}

void appTickSleepTimer(App *app, uint64_t now_ns)
{
    if (!app->sleep_deadline_ns || now_ns < app->sleep_deadline_ns)
        return;
    app->sleep_deadline_ns = 0;
    app->sleep_minutes = 0;
    playerFadeOutAndPause(SLEEP_FADE_SECONDS);
    appToast(app, "Sleep timer: pausing.");
}
