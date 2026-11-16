// The Settings page and the sleep timer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "background.h"
#include "keyboard_system.h"
#include "nowstyle.h"
#include "player.h"
#include "resume.h"
#include "screen.h"
#include "theme.h"

#define SLEEP_FADE_SECONDS 10.0
#define NS_PER_MINUTE 60000000000ull

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

void appOpenSettings(App *app)
{
    app->settings_open = true;
    app->settings_page = SETTINGS_PAGE_MAIN;
    app->settings_cursor = SETTING_OPEN_PLAYBACK;
}

static void openPage(App *app, SettingsPage page)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    settingsPageRows(page, rows);
    app->settings_page = page;
    app->settings_cursor = rows[0];
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

void appApplyAudioSettings(App *app)
{
    int gains[EQ_BANDS];
    if (!eqPresetGains((EqPreset)app->config.eq_preset, gains))
        memcpy(gains, app->config.eq_gains, sizeof(gains));  // Custom.
    playerSetEqualizer(app->config.eq_on, gains);
    playerSetReplayGain((ReplayGainMode)app->config.replaygain);
}

void appActivateSetting(App *app, SettingRow row)
{
    if (settingsRowPage(row) != SETTINGS_PAGE_MAIN) {
        openPage(app, settingsRowPage(row));
    } else if (row == SETTING_SLEEP) {
        typeSleepMinutes(app);
    } else if (row == SETTING_EQUALIZER) {
        app->eq_open = true;  // The editor opens over Settings.
    } else if (row == SETTING_ABOUT) {
        app->about_open = true;
    } else if (row == SETTING_LIBRARY) {
        appOpenFolderPicker(app, false);
    } else {
        appChangeSetting(app, row, 1);
    }
}

// Off, then each preset, then Custom.
static void stepEqualizer(App *app, int delta)
{
    int count = EQ_PRESET_COUNT + 1;
    int index = app->config.eq_on ? app->config.eq_preset + 1 : 0;
    index = ((index + delta) % count + count) % count;
    app->config.eq_on = index > 0;
    if (index > 0)
        app->config.eq_preset = index - 1;
}

// Steps through "the cover colors" and the images in <music>/Backgrounds.
static void stepBackground(App *app, int delta)
{
    static char names[BACKGROUND_MAX_IMAGES][BACKGROUND_NAME_MAX];
    int count = (int)backgroundList(app->config.library, names, BACKGROUND_MAX_IMAGES);
    if (count == 0) {
        app->config.background[0] = '\0';
        appToast(app, app->config.library[0] ? "Put JPEG or PNG images in a Backgrounds folder in your music folder."
                                             : "Choose your music folder first.");
        return;
    }
    int index = 0;  // 0 is the cover colors, then the images.
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], app->config.background) == 0)
            index = i + 1;
    }
    index = ((index + delta) % (count + 1) + count + 1) % (count + 1);
    snprintf(app->config.background, sizeof(app->config.background), "%s", index ? names[index - 1] : "");
    if (!backgroundLoad(app->config.library, app->config.background, app->config.background_blur)) {
        char message[BACKGROUND_NAME_MAX + 80];
        snprintf(message, sizeof(message), "Could not read %s (JPEG or PNG, up to 50 megapixels).",
                 app->config.background);
        appToast(app, message);
        app->config.background[0] = '\0';  // Not kept: it would fail again at every start.
    }
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
    case SETTING_EQUALIZER:
        stepEqualizer(app, delta);
        appApplyAudioSettings(app);
        configSave(&app->config);
        break;
    case SETTING_REPLAYGAIN:
        app->config.replaygain = ((app->config.replaygain + delta) % REPLAYGAIN_MODE_COUNT + REPLAYGAIN_MODE_COUNT) %
                                 REPLAYGAIN_MODE_COUNT;
        appApplyAudioSettings(app);
        configSave(&app->config);
        break;
    case SETTING_ACCENT:
        app->config.accent = ((app->config.accent + delta) % THEME_ACCENT_COUNT + THEME_ACCENT_COUNT) % THEME_ACCENT_COUNT;
        configSave(&app->config);
        break;
    case SETTING_NOW_STYLE:
        app->config.now_style = ((app->config.now_style + delta) % NOW_STYLE_COUNT + NOW_STYLE_COUNT) % NOW_STYLE_COUNT;
        configSave(&app->config);
        break;
    case SETTING_BACKGROUND:
        stepBackground(app, delta);
        configSave(&app->config);
        break;
    case SETTING_BACKGROUND_BLUR:
        app->config.background_blur = ((app->config.background_blur + delta) % BLUR_COUNT + BLUR_COUNT) % BLUR_COUNT;
        backgroundSetBlur(app->config.background_blur);  // From the copy kept at the screen's size: quick.
        configSave(&app->config);
        break;
    case SETTING_SCREEN_OFF:
        app->config.screen_off_minutes = settingsScreenOffStep(app->config.screen_off_minutes, delta);
        configSave(&app->config);
        break;
    default:
        break;
    }
}

void appHandleSettings(App *app, uint64_t pressed)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows(app->settings_page, rows), index = 0;
    for (int i = 0; i < count; i++) {
        if (rows[i] == app->settings_cursor)
            index = i;
    }
    if (pressed & BUTTONS_UP)
        index = index > 0 ? index - 1 : 0;
    if (pressed & BUTTONS_DOWN)
        index = index + 1 < count ? index + 1 : count - 1;
    app->settings_cursor = rows[index];
    bool opens = settingsRowPage(app->settings_cursor) != SETTINGS_PAGE_MAIN;
    if (pressed & HidNpadButton_A || (opens && pressed & BUTTONS_RIGHT)) {
        appActivateSetting(app, app->settings_cursor);
        return;
    }
    if (pressed & BUTTONS_RIGHT)
        appChangeSetting(app, app->settings_cursor, 1);
    if (pressed & BUTTONS_LEFT)
        appChangeSetting(app, app->settings_cursor, -1);
    if (pressed & HidNpadButton_Plus) {
        app->settings_open = false;
    } else if (pressed & HidNpadButton_B) {
        if (app->settings_page == SETTINGS_PAGE_MAIN) {
            app->settings_open = false;
        } else {  // Back to the main page, on the row of the page left.
            app->settings_cursor = settingsPageRow(app->settings_page);
            app->settings_page = SETTINGS_PAGE_MAIN;
        }
    }
}

void appScreenOff(App *app)
{
    app->screen_off = true;
    screenSetOn(false);
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
