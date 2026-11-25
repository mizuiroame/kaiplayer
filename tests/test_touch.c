// Tests for touch gestures, touch targets, settings and the clock format.
#include <string.h>

#include "check.h"
#include "settings.h"
#include "touch.h"
#include "ui_hit.h"
#include "util.h"

#define MS 1000000ull

static void testTap(void)
{
    TouchTracker t;
    touchReset(&t);
    TouchEvent e = touchUpdate(&t, true, 100, 200, 0);
    CHECK(e.type == TOUCH_PRESS && e.x == 100 && e.y == 200);  // Down: a glide stops.
    e = touchUpdate(&t, true, 105, 203, 50 * MS);  // Within the slop.
    CHECK(e.type == TOUCH_NONE);
    e = touchUpdate(&t, false, 0, 0, 120 * MS);
    CHECK(e.type == TOUCH_TAP && e.x == 100 && e.y == 200);
    e = touchUpdate(&t, false, 0, 0, 140 * MS);    // Nothing after the release.
    CHECK(e.type == TOUCH_NONE);

    touchUpdate(&t, true, 10, 10, 0);               // Held too long: no tap.
    e = touchUpdate(&t, false, 0, 0, 2000 * MS);
    CHECK(e.type == TOUCH_NONE);
}

static void testDrag(void)
{
    TouchTracker t;
    touchReset(&t);
    touchUpdate(&t, true, 300, 400, 0);
    TouchEvent e = touchUpdate(&t, true, 300, 360, 16 * MS);  // Past the slop: a drag.
    CHECK(e.type == TOUCH_DRAG && e.dy == -40 && e.start_y == 400);
    e = touchUpdate(&t, true, 300, 350, 32 * MS);
    CHECK(e.type == TOUCH_DRAG && e.dy == -10);
    e = touchUpdate(&t, false, 0, 0, 48 * MS);                 // A drag ends where it was released.
    CHECK(e.type == TOUCH_DRAG_END && e.x == 300 && e.y == 350 && e.start_y == 400);
}

static void testHits(void)
{
    uiHitReset();
    CHECK(uiHitFind(10, 10) == NULL);
    uiHitAdd(0, 0, 1280, 720, HIT_BACKDROP, 0);
    uiHitAdd(100, 100, 50, 40, HIT_MENU_ITEM, 3);
    uiHitAdd(0, 0, 0, 10, HIT_ROW, 1);  // Empty: ignored.
    const HitRegion *r = uiHitFind(120, 110);
    CHECK(r && r->kind == HIT_MENU_ITEM && r->arg == 3);  // Topmost wins.
    r = uiHitFind(150, 110);                               // Right edge is outside.
    CHECK(r && r->kind == HIT_BACKDROP);
    CHECK(uiHitFind(-1, 5) == NULL);
    for (int i = 0; i < HIT_MAX_REGIONS + 10; i++)
        uiHitAdd(i, 0, 1, 1, HIT_ROW, i);                  // Extra regions are dropped.
    uiHitReset();
    CHECK(uiHitFind(120, 110) == NULL);
}

static void testSettings(void)
{
    CHECK(settingsSleepStep(0, 1) == 5 && settingsSleepStep(5, 1) == 10);
    CHECK(settingsSleepStep(0, -1) == 0);                       // Stays off.
    CHECK(settingsSleepStep(7, 1) == 10 && settingsSleepStep(7, -1) == 5);
    CHECK(settingsSleepStep(SLEEP_MAX_MINUTES, 1) == SLEEP_MAX_MINUTES);
    CHECK(settingsSleepClamp(-3) == 0 && settingsSleepClamp(99999) == SLEEP_MAX_MINUTES);
    CHECK(settingsSleepClamp(42) == 42);
    CHECK(settingsRowEditable(SETTING_SLEEP) && !settingsRowEditable(SETTING_ABOUT));
    CHECK(strcmp(settingsRowLabel(SETTING_CLOCK), "Clock") == 0 && settingsRowLabel(SETTING_COUNT)[0] == '\0');

    // Every row is on exactly one page; the main page opens the others.
    int seen[SETTING_COUNT] = {0};
    for (int page = 0; page < SETTINGS_PAGE_COUNT; page++) {
        SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
        int count = settingsPageRows((SettingsPage)page, rows);
        CHECK(count > 0 && count <= SETTINGS_PAGE_MAX_ROWS);
        for (int i = 0; i < count; i++)
            seen[rows[i]]++;
        if (page != SETTINGS_PAGE_MAIN) {
            SettingRow opener = settingsPageRow((SettingsPage)page);
            CHECK(settingsRowPage(opener) == (SettingsPage)page && !settingsRowEditable(opener));
        }
    }
    for (int row = 0; row < SETTING_COUNT; row++)
        CHECK(seen[row] == 1);
    CHECK(strcmp(settingsPageTitle(SETTINGS_PAGE_DISPLAY), "Display") == 0);
    CHECK(strcmp(settingsPageTitle(SETTINGS_PAGE_LIBRARY), "Library") == 0);

    // The pages as in KaiVideo and KaiRadio: Library, the app's sources (the network folder, as KaiVideo's, and
    // Navidrome), Playback, Display, then About.
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    CHECK(settingsPageRows(SETTINGS_PAGE_MAIN, rows) == 6);
    CHECK(rows[0] == SETTING_OPEN_LIBRARY && rows[1] == SETTING_OPEN_NETWORK && rows[2] == SETTING_OPEN_SERVER &&
          rows[3] == SETTING_OPEN_PLAYBACK && rows[4] == SETTING_OPEN_DISPLAY && rows[5] == SETTING_ABOUT);
    // Every source's page in one order, as the other Kai apps: address, sign-in, options (color last), test, forget.
    CHECK(settingsPageRows(SETTINGS_PAGE_SERVER, rows) == 6);
    CHECK(rows[0] == SETTING_SRV_ADDRESS && rows[1] == SETTING_SRV_ACCOUNT && rows[2] == SETTING_SRV_CERTIFICATE &&
          rows[3] == SETTING_SRV_COLOR && rows[4] == SETTING_SRV_TEST && rows[5] == SETTING_SRV_FORGET);
    CHECK(settingsRowEditable(SETTING_SRV_COLOR) && settingsRowEditable(SETTING_NET_COLOR));
    CHECK(settingsRowPage(SETTING_OPEN_SERVER) == SETTINGS_PAGE_SERVER &&
          settingsPageRow(SETTINGS_PAGE_SERVER) == SETTING_OPEN_SERVER);
    CHECK(settingsRowEditable(SETTING_SRV_CERTIFICATE) && !settingsRowEditable(SETTING_SRV_ADDRESS) &&
          !settingsRowEditable(SETTING_SRV_ACCOUNT) && !settingsRowEditable(SETTING_SRV_TEST) &&
          !settingsRowEditable(SETTING_SRV_FORGET));
    CHECK(strcmp(settingsPageTitle(SETTINGS_PAGE_SERVER), "Navidrome") == 0 && settingsPageNote(SETTINGS_PAGE_SERVER));
    CHECK(settingsPageRows(SETTINGS_PAGE_NETWORK, rows) == 8);
    CHECK(rows[0] == SETTING_NET_ADDRESS && rows[1] == SETTING_NET_ACCOUNT && rows[2] == SETTING_NET_REMEMBER &&
          rows[3] == SETTING_NET_UNSIGNED && rows[4] == SETTING_NET_CERTIFICATE && rows[5] == SETTING_NET_COLOR &&
          rows[6] == SETTING_NET_TEST && rows[7] == SETTING_NET_FORGET);
    CHECK(!settingsRowEditable(SETTING_NET_ADDRESS) && !settingsRowEditable(SETTING_NET_FORGET));
    // The switches change with the arrows too, as the server's Certificate check.
    CHECK(settingsRowEditable(SETTING_NET_REMEMBER) && settingsRowEditable(SETTING_NET_UNSIGNED) &&
          settingsRowEditable(SETTING_NET_CERTIFICATE));
    CHECK(strcmp(settingsRowLabel(SETTING_NET_CERTIFICATE), "Certificate check") == 0);
    CHECK(strcmp(settingsRowLabel(SETTING_NET_UNSIGNED), "Allow without signing (old NAS)") == 0);
    CHECK(strcmp(settingsPageTitle(SETTINGS_PAGE_NETWORK), "Network folder") == 0);
    CHECK(settingsPageNote(SETTINGS_PAGE_NETWORK) && !settingsPageNote(SETTINGS_PAGE_MAIN));
    // About opens a page: the credits, and updates (no page of their own).
    CHECK(settingsRowPage(SETTING_ABOUT) == SETTINGS_PAGE_ABOUT && settingsPageRow(SETTINGS_PAGE_ABOUT) == SETTING_ABOUT);
    CHECK(settingsPageRows(SETTINGS_PAGE_ABOUT, rows) == 3 && rows[0] == SETTING_CREDITS &&
          rows[1] == SETTING_UPDATE_AUTO && rows[2] == SETTING_UPDATE_NOW);
    CHECK(settingsRowEditable(SETTING_UPDATE_AUTO) && !settingsRowEditable(SETTING_UPDATE_NOW) &&
          !settingsRowEditable(SETTING_CREDITS) && !settingsRowEditable(SETTING_ABOUT));
    CHECK(strcmp(settingsPageTitle(SETTINGS_PAGE_ABOUT), "About") == 0);
    CHECK(settingsPageRows(SETTINGS_PAGE_LIBRARY, rows) == 2);
    CHECK(rows[0] == SETTING_LIBRARY && rows[1] == SETTING_RESCAN);
    CHECK(!settingsRowEditable(SETTING_RESCAN) && !settingsRowEditable(SETTING_LIBRARY));
    CHECK(settingsPageRows(SETTINGS_PAGE_PLAYBACK, rows) == 5);
    CHECK(rows[0] == SETTING_SLEEP && rows[1] == SETTING_RESUME && rows[2] == SETTING_SCREEN_OFF);
    CHECK(settingsPageRows(SETTINGS_PAGE_DISPLAY, rows) == 6);
    CHECK(rows[0] == SETTING_LANGUAGE && rows[5] == SETTING_NOW_STYLE);
    CHECK(strcmp(settingsRowLabel(SETTING_RESCAN), "Update the library") == 0);

    char out[16];
    utilFormatClock(out, sizeof(out), 21, 7, true);
    CHECK(strcmp(out, "21:07") == 0);
    utilFormatClock(out, sizeof(out), 21, 7, false);
    CHECK(strcmp(out, "9:07 PM") == 0);
    utilFormatClock(out, sizeof(out), 0, 30, false);
    CHECK(strcmp(out, "12:30 AM") == 0);
    utilFormatClock(out, sizeof(out), 12, 0, false);
    CHECK(strcmp(out, "12:00 PM") == 0);
}

void testTouch(void)
{
    testTap();
    testDrag();
    testHits();
    testSettings();
}
