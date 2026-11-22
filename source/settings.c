#include "settings.h"

#include "lang.h"

static const char *const ROW_LABELS[SETTING_COUNT] = {
    N_("Music folder"), N_("Sleep timer"), N_("Resume where you left off"), N_("Clock"), N_("Equalizer"),
    N_("ReplayGain (even loudness)"), N_("Accent color"), N_("Now Playing style"), N_("Background"),
    N_("Background blur"), N_("Screen off while playing"), N_("About"), N_("Playback"), N_("Display"),
    N_("Language"), N_("Library"), N_("Update the library"), N_("Network folder"), N_("Address"),
    N_("User and password"), N_("Test the connection"), N_("Forget this folder"), N_("Navidrome"), N_("Server"),
    N_("User and password"), N_("Test the connection"), N_("Certificate check"), N_("Forget this server"),
};

// The same pages, in the same order, as KaiVideo's and KaiRadio's Settings.
static const SettingRow MAIN_ROWS[] = {SETTING_OPEN_LIBRARY, SETTING_OPEN_NETWORK, SETTING_OPEN_SERVER,
                                       SETTING_OPEN_PLAYBACK, SETTING_OPEN_DISPLAY, SETTING_ABOUT};
static const SettingRow LIBRARY_ROWS[] = {SETTING_LIBRARY, SETTING_RESCAN};
static const SettingRow NETWORK_ROWS[] = {SETTING_NET_ADDRESS, SETTING_NET_ACCOUNT, SETTING_NET_TEST,
                                          SETTING_NET_FORGET};
static const SettingRow SERVER_ROWS[] = {SETTING_SRV_ADDRESS, SETTING_SRV_ACCOUNT, SETTING_SRV_TEST,
                                         SETTING_SRV_CERTIFICATE, SETTING_SRV_FORGET};
static const SettingRow PLAYBACK_ROWS[] = {SETTING_SLEEP, SETTING_RESUME, SETTING_SCREEN_OFF, SETTING_EQUALIZER,
                                           SETTING_REPLAYGAIN};
static const SettingRow DISPLAY_ROWS[] = {SETTING_LANGUAGE,        SETTING_ACCENT, SETTING_BACKGROUND,
                                          SETTING_BACKGROUND_BLUR, SETTING_CLOCK,  SETTING_NOW_STYLE};

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

int settingsPageRows(SettingsPage page, SettingRow rows[SETTINGS_PAGE_MAX_ROWS])
{
    const SettingRow *from = MAIN_ROWS;
    int count = COUNT_OF(MAIN_ROWS);
    switch (page) {
    case SETTINGS_PAGE_LIBRARY: from = LIBRARY_ROWS; count = COUNT_OF(LIBRARY_ROWS); break;
    case SETTINGS_PAGE_NETWORK: from = NETWORK_ROWS; count = COUNT_OF(NETWORK_ROWS); break;
    case SETTINGS_PAGE_SERVER: from = SERVER_ROWS; count = COUNT_OF(SERVER_ROWS); break;
    case SETTINGS_PAGE_PLAYBACK: from = PLAYBACK_ROWS; count = COUNT_OF(PLAYBACK_ROWS); break;
    case SETTINGS_PAGE_DISPLAY: from = DISPLAY_ROWS; count = COUNT_OF(DISPLAY_ROWS); break;
    default: break;
    }
    for (int i = 0; i < count; i++)
        rows[i] = from[i];
    return count;
}

SettingsPage settingsRowPage(SettingRow row)
{
    switch (row) {
    case SETTING_OPEN_LIBRARY: return SETTINGS_PAGE_LIBRARY;
    case SETTING_OPEN_NETWORK: return SETTINGS_PAGE_NETWORK;
    case SETTING_OPEN_SERVER: return SETTINGS_PAGE_SERVER;
    case SETTING_OPEN_PLAYBACK: return SETTINGS_PAGE_PLAYBACK;
    case SETTING_OPEN_DISPLAY: return SETTINGS_PAGE_DISPLAY;
    default: return SETTINGS_PAGE_MAIN;
    }
}

SettingRow settingsPageRow(SettingsPage page)
{
    switch (page) {
    case SETTINGS_PAGE_NETWORK: return SETTING_OPEN_NETWORK;
    case SETTINGS_PAGE_SERVER: return SETTING_OPEN_SERVER;
    case SETTINGS_PAGE_PLAYBACK: return SETTING_OPEN_PLAYBACK;
    case SETTINGS_PAGE_DISPLAY: return SETTING_OPEN_DISPLAY;
    default: return SETTING_OPEN_LIBRARY;
    }
}

const char *settingsPageTitle(SettingsPage page)
{
    switch (page) {
    case SETTINGS_PAGE_LIBRARY: return T("Library");
    case SETTINGS_PAGE_NETWORK: return T("Network folder");
    case SETTINGS_PAGE_SERVER: return T("Navidrome");
    case SETTINGS_PAGE_PLAYBACK: return T("Playback");
    case SETTINGS_PAGE_DISPLAY: return T("Display");
    default: return T("Settings");
    }
}

const char *settingsPageNote(SettingsPage page)
{
    if (page == SETTINGS_PAGE_SERVER)
        return T("A Navidrome server, or another with the Subsonic API. On Home and Library, X switches to its music.");
    return page == SETTINGS_PAGE_NETWORK
               ? T("A shared folder (SMB) or WebDAV folder on your network. On Home and Library, X switches to its music.")
               : NULL;
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
    return row >= 0 && row < SETTING_COUNT ? T(ROW_LABELS[row]) : "";
}

bool settingsRowEditable(SettingRow row)
{
    // Those A opens or does: About, the folder picker, Update the library, the network folder's, the
    // server's (but its certificate check) and the pages.
    return row != SETTING_ABOUT && row != SETTING_LIBRARY && row != SETTING_RESCAN && row != SETTING_NET_ADDRESS &&
           row != SETTING_NET_ACCOUNT && row != SETTING_NET_TEST && row != SETTING_NET_FORGET &&
           row != SETTING_SRV_ADDRESS && row != SETTING_SRV_ACCOUNT && row != SETTING_SRV_TEST &&
           row != SETTING_SRV_FORGET && settingsRowPage(row) == SETTINGS_PAGE_MAIN;
}
