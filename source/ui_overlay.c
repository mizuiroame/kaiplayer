// The + menu, Settings and the on-screen keyboard, drawn over the current tab.
#include <stdio.h>

#include "settings.h"
#include "ui_common.h"
#include "ui_hit.h"

#ifndef KAIPLAYER_VERSION
#define KAIPLAYER_VERSION "dev"
#endif

#define DIM_ALPHA 160
#define MENU_W 640
#define MENU_ROW_H 48
#define MENU_VISIBLE 9
#define SETTINGS_W 760
#define SETTINGS_ROW_H 56
#define KEY_W 76
#define KEY_H 56
#define KEY_GAP 8
#define KEYS_W (KEYBOARD_COLS * KEY_W + (KEYBOARD_COLS - 1) * KEY_GAP)
#define PANEL_PAD 32
#define NS_PER_MINUTE 60000000000ull

// Darkens the screen; a tap there closes the overlay.
static void dimScreen(void)
{
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, COLOR_SHADOW, DIM_ALPHA);
    uiHitAdd(0, 0, GFX_WIDTH, GFX_HEIGHT, HIT_BACKDROP, 0);
}

static void panel(int x, int y, int w, int h)
{
    gfxFillRoundRect(x, y, w, h, 16, COLOR_SURFACE);
    uiHitAdd(x, y, w, h, HIT_LIST, 0);  // Taps on the panel itself do nothing.
}

static int firstVisibleItem(int cursor, int count)
{
    if (count <= MENU_VISIBLE || cursor < MENU_VISIBLE / 2)
        return 0;
    int first = cursor - MENU_VISIBLE / 2;
    return first + MENU_VISIBLE > count ? count - MENU_VISIBLE : first;
}

static void drawRowHighlight(int x, int y, int w, int h)
{
    gfxFillRoundRect(x, y, w, h, 10, COLOR_SURFACE_HI);
    gfxFillRect(x, y + 10, 3, h - 20, COLOR_ACCENT);
}

static void drawMenu(const Menu *m)
{
    int rows = m->count < MENU_VISIBLE ? m->count : MENU_VISIBLE;
    int h = 76 + rows * MENU_ROW_H + 20;
    int x = (GFX_WIDTH - MENU_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, MENU_W, h);
    fontDrawClipped(FONT_BOLD, 22, x + PANEL_PAD, y + 24, MENU_W - 2 * PANEL_PAD, m->title, COLOR_TEXT);

    int first = firstVisibleItem(m->cursor, m->count);
    for (int i = first; i < m->count && i < first + rows; i++) {
        int row_y = y + 76 + (i - first) * MENU_ROW_H;
        bool selected = i == m->cursor;
        if (selected)
            drawRowHighlight(x + 16, row_y, MENU_W - 32, MENU_ROW_H - 6);
        fontDrawClipped(selected ? FONT_BOLD : FONT_REGULAR, 21, x + PANEL_PAD, row_y + 9,
                        MENU_W - 2 * PANEL_PAD, m->items[i].label, COLOR_TEXT);
        uiHitAdd(x + 16, row_y, MENU_W - 32, MENU_ROW_H - 6, HIT_MENU_ITEM, i);
    }
    if (m->count > rows) {
        char more[32];
        snprintf(more, sizeof(more), "%d / %d", m->cursor + 1, m->count);
        fontDraw(FONT_REGULAR, 16, x + MENU_W - PANEL_PAD - fontTextWidth(FONT_REGULAR, 16, more), y + 28,
                 more, COLOR_DIM);
    }
}

static void settingValue(const App *app, SettingRow row, uint64_t now_ns, char *out, size_t size)
{
    switch (row) {
    case SETTING_SLEEP:
        if (!app->sleep_deadline_ns || app->sleep_deadline_ns <= now_ns) {
            snprintf(out, size, "Off");
        } else {
            unsigned left = (unsigned)((app->sleep_deadline_ns - now_ns + NS_PER_MINUTE - 1) / NS_PER_MINUTE);
            snprintf(out, size, "%d min  (%u left)", settingsSleepMinutes(app->sleep_option), left);
        }
        break;
    case SETTING_RESUME: snprintf(out, size, "%s", app->config.resume ? "On" : "Off"); break;
    case SETTING_CLOCK: snprintf(out, size, "%s", app->config.clock_24h ? "24-hour" : "12-hour"); break;
    default: snprintf(out, size, "KaiPlayer %s", KAIPLAYER_VERSION); break;
    }
}

static void drawSettings(const App *app, uint64_t now_ns)
{
    int h = 76 + SETTING_COUNT * SETTINGS_ROW_H + 60;
    int x = (GFX_WIDTH - SETTINGS_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, SETTINGS_W, h);
    fontDraw(FONT_BOLD, 24, x + PANEL_PAD, y + 22, "Settings", COLOR_TEXT);

    char value[64];
    for (int i = 0; i < SETTING_COUNT; i++) {
        int row_y = y + 76 + i * SETTINGS_ROW_H;
        bool selected = (int)app->settings_cursor == i;
        bool editable = settingsRowEditable((SettingRow)i);
        if (selected)
            drawRowHighlight(x + 16, row_y, SETTINGS_W - 32, SETTINGS_ROW_H - 6);
        fontDraw(selected ? FONT_BOLD : FONT_REGULAR, 21, x + PANEL_PAD, row_y + 12,
                 settingsRowLabel((SettingRow)i), COLOR_TEXT);
        settingValue(app, (SettingRow)i, now_ns, value, sizeof(value));
        int value_w = fontTextWidth(FONT_BOLD, 20, value);
        int value_x = x + SETTINGS_W - PANEL_PAD - value_w - (editable ? 24 : 0);
        fontDraw(FONT_BOLD, 20, value_x, row_y + 13, value, editable ? COLOR_ACCENT : COLOR_DIM);
        if (editable) {
            gfxFillTriangleLeft(value_x - 22, row_y + 17, 10, 16, COLOR_DIM);
            gfxFillTriangleRight(x + SETTINGS_W - PANEL_PAD - 12, row_y + 17, 10, 16, COLOR_DIM);
            uiHitAdd(x + 16, row_y, SETTINGS_W - 32, SETTINGS_ROW_H - 6, HIT_SETTING, i);
        }
    }
    fontDrawClipped(FONT_REGULAR, 16, x + PANEL_PAD, y + h - 40, SETTINGS_W - 2 * PANEL_PAD,
                    "Open source (MIT)  ·  github.com/mizuiroame/kaiplayer", COLOR_FAINT);
}

static void drawKey(int x, int y, int w, const char *label, bool selected, bool lit, int hit_arg)
{
    gfxFillRoundRect(x, y, w, KEY_H, 10, selected ? COLOR_ACCENT : COLOR_SURFACE_HI);
    Color text = selected ? COLOR_BG : lit ? COLOR_ACCENT : COLOR_TEXT;
    int label_w = fontTextWidth(FONT_BOLD, 22, label);
    fontDraw(FONT_BOLD, 22, x + (w - label_w) / 2, y + 13, label, text);
    uiHitAdd(x, y, w, KEY_H, HIT_KEY, hit_arg);
}

static void drawKeyboard(const App *app, uint64_t now_ns)
{
    const Keyboard *k = &app->keyboard;
    int w = KEYS_W + 2 * PANEL_PAD;
    int h = 150 + KEYBOARD_ROWS * (KEY_H + KEY_GAP) + 52;
    int x = (GFX_WIDTH - w) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, w, h);
    fontDraw(FONT_BOLD, 22, x + PANEL_PAD, y + 22, app->keyboard_title, COLOR_TEXT);

    // The name being typed, with a caret.
    char shown[KEYBOARD_MAX + 2];
    snprintf(shown, sizeof(shown), "%s|", k->text);
    gfxFillRoundRect(x + PANEL_PAD, y + 64, KEYS_W, 60, 12, COLOR_BG);
    fontDrawClipped(FONT_REGULAR, 26, x + PANEL_PAD + 16, y + 76, KEYS_W - 32, shown, COLOR_TEXT);

    int keys_y = y + 150;
    char label[2] = {0, 0};
    for (int r = 0; r < KEYBOARD_CHAR_ROWS; r++) {
        for (int c = 0; c < KEYBOARD_COLS; c++) {
            label[0] = keyboardCharAt(k, r, c);
            drawKey(x + PANEL_PAD + c * (KEY_W + KEY_GAP), keys_y + r * (KEY_H + KEY_GAP), KEY_W, label,
                    k->row == r && k->col == c, false, r * 16 + c);
        }
    }
    int action_w = (KEYS_W - (KEYBOARD_ACTIONS - 1) * KEY_GAP) / KEYBOARD_ACTIONS;
    int action_y = keys_y + KEYBOARD_CHAR_ROWS * (KEY_H + KEY_GAP);
    for (int a = 0; a < KEYBOARD_ACTIONS; a++) {
        drawKey(x + PANEL_PAD + a * (action_w + KEY_GAP), action_y, action_w,
                keyboardActionLabel((KeyboardAction)a), k->row == KEYBOARD_CHAR_ROWS && k->col == a,
                a == KEY_ACTION_SHIFT && k->shift, KEYBOARD_CHAR_ROWS * 16 + a);
    }
    // Messages such as an invalid name show here, above the dimmed screen.
    if (app->toast[0] && now_ns < app->toast_until_ns)
        fontDrawClipped(FONT_REGULAR, 18, x + PANEL_PAD, action_y + KEY_H + 14, KEYS_W, app->toast, COLOR_ERROR);
}

void uiDrawOverlay(const App *app, uint64_t now_ns)
{
    if (app->keyboard_open)
        drawKeyboard(app, now_ns);
    else if (app->menu.open)
        drawMenu(&app->menu);
    else if (app->settings_open)
        drawSettings(app, now_ns);
}
