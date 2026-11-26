// The Settings page: its rows, grouped in pages, and the sleep timer choices.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SETTING_LIBRARY,        // The music folder: opens the folder picker.
    SETTING_SLEEP,
    SETTING_RESUME,
    SETTING_CLOCK,
    SETTING_EQUALIZER,
    SETTING_REPLAYGAIN,
    SETTING_ACCENT,
    SETTING_NOW_STYLE,
    SETTING_BACKGROUND,
    SETTING_BACKGROUND_BLUR,
    SETTING_SCREEN_OFF,
    SETTING_ABOUT,
    SETTING_OPEN_PLAYBACK,  // Rows that open a page.
    SETTING_OPEN_DISPLAY,
    SETTING_LANGUAGE,       // The language KaiPlayer is shown in (languages.h).
    SETTING_OPEN_LIBRARY,
    SETTING_RESCAN,         // Looks for music again, as the + menu's "Update the library".
    SETTING_OPEN_NETWORK,   // Opens the network folder's page.
    SETTING_NET_ADDRESS,    // Types the network folder's address (dav.h).
    SETTING_NET_ACCOUNT,    // Types its user name and password.
    SETTING_NET_TEST,       // Tries it, and says what's at its top.
    SETTING_NET_FORGET,     // Removes its address, user and password.
    SETTING_OPEN_SERVER,    // Opens the Navidrome server's page.
    SETTING_SRV_ADDRESS,    // Types the server's address (subsonic.h).
    SETTING_SRV_ACCOUNT,    // Types its user name and password, and signs in.
    SETTING_SRV_TEST,       // Tries it, and says what it is.
    SETTING_SRV_CERTIFICATE,// Whether an https server's certificate must be trusted.
    SETTING_SRV_FORGET,     // Removes its address and sign-in.
    SETTING_NET_REMEMBER,   // Whether the network folder's password is kept on the card.
    SETTING_NET_UNSIGNED,   // Whether an SMB server that doesn't sign is used (an old NAS).
    SETTING_CREDITS,        // About: credits and licenses (the About panel).
    SETTING_UPDATE_AUTO,    // About: look for a new version once a day.
    SETTING_UPDATE_NOW,     // About: look now, or install the one found.
    SETTING_NET_COLOR,      // The color the network folder wears: its own, or an accent.
    SETTING_SRV_COLOR,      // Navidrome's.
    SETTING_NET_CERTIFICATE,// Whether an https network folder's certificate must be trusted.
    SETTING_SRV_API_KEY,    // Types an API key instead of a user and password, and signs in.
    SETTING_OPEN_ARCHIVE,   // Opens the Internet Archive's page.
    SETTING_IA_COLOR,       // The color the Internet Archive wears.
    SETTING_COUNT,
} SettingRow;

// Settings opens on the main page; Library, Network folder, Navidrome, Internet Archive, Playback and Display
// hold the rest.
typedef enum {
    SETTINGS_PAGE_MAIN,
    SETTINGS_PAGE_LIBRARY,
    SETTINGS_PAGE_NETWORK,
    SETTINGS_PAGE_SERVER,
    SETTINGS_PAGE_PLAYBACK,
    SETTINGS_PAGE_DISPLAY,
    SETTINGS_PAGE_ABOUT,
    SETTINGS_PAGE_ARCHIVE,
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
// A line about the page, at the panel's foot; NULL for none.
const char *settingsPageNote(SettingsPage page);

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
