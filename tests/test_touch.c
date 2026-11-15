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
    CHECK(e.type == TOUCH_NONE);
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
