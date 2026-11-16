// The + menu, Settings and the on-screen keyboard, drawn over the current tab.
#include <stdio.h>

#include "settings.h"
#include "theme.h"
#include "ui_buttons.h"
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
            snprintf(out, size, "%d min  (%u left)", app->sleep_minutes, left);
        }
        break;
    case SETTING_RESUME: {
        static const char *const MODES[RESUME_MODE_COUNT] = {"Off", "Paused", "Keep playing"};
        snprintf(out, size, "%s", MODES[app->config.resume % RESUME_MODE_COUNT]);
        break;
    }
    case SETTING_CLOCK: snprintf(out, size, "%s", app->config.clock_24h ? "24-hour" : "12-hour"); break;
    case SETTING_EQUALIZER:
        snprintf(out, size, "%s", app->config.eq_on ? eqPresetName((EqPreset)app->config.eq_preset) : "Off");
        break;
    case SETTING_REPLAYGAIN: {
        static const char *const MODES[REPLAYGAIN_MODE_COUNT] = {"Off", "Track", "Album", "Auto"};
        snprintf(out, size, "%s", MODES[app->config.replaygain % REPLAYGAIN_MODE_COUNT]);
        break;
    }
    case SETTING_ACCENT: snprintf(out, size, "%s", themeAccentName(app->config.accent)); break;
    case SETTING_SCREEN_OFF:
        if (app->config.screen_off_minutes)
            snprintf(out, size, "After %d min idle", app->config.screen_off_minutes);
        else
            snprintf(out, size, "Never");
        break;
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
        } else if (selected) {  // About: A opens it.
            uiDrawButton(value_x - 34, (float)row_y + 25.0f, "A", COLOR_SURFACE, COLOR_TEXT);
        }
        uiHitAdd(x + 16, row_y, SETTINGS_W - 32, SETTINGS_ROW_H - 6, HIT_SETTING, i);
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

#define EQ_W 940
#define EQ_H 500
#define EQ_SLIDER_H 250
#define EQ_COLUMN_W 80

static void drawEqualizer(const App *app)
{
    int x = (GFX_WIDTH - EQ_W) / 2, y = (GFX_HEIGHT - EQ_H) / 2;
    dimScreen();
    panel(x, y, EQ_W, EQ_H);
    fontDraw(FONT_BOLD, 24, x + PANEL_PAD, y + 22, "Equalizer", COLOR_TEXT);

    // Preset and on/off pills, top right.
    char preset[48];
    snprintf(preset, sizeof(preset), "X  %s", eqPresetName((EqPreset)app->config.eq_preset));
    const char *toggle = app->config.eq_on ? "Y  On" : "Y  Off";
    int px = x + EQ_W - PANEL_PAD - uiPillWidth(toggle);
    uiDrawPill(px, y + 22, toggle, app->config.eq_on);
    uiHitAdd(px, y + 14, uiPillWidth(toggle), 46, HIT_EQ_TOGGLE, 0);
    px -= uiPillWidth(preset) + 12;
    uiDrawPill(px, y + 22, preset, app->config.eq_on);
    uiHitAdd(px, y + 14, uiPillWidth(preset), 46, HIT_EQ_PRESET, 0);

    int gains[EQ_BANDS];
    if (!eqPresetGains((EqPreset)app->config.eq_preset, gains))
        for (int b = 0; b < EQ_BANDS; b++)
            gains[b] = app->config.eq_gains[b];
    int top = y + 120, zero_y = top + EQ_SLIDER_H / 2;
    int first_x = x + (EQ_W - EQ_BANDS * EQ_COLUMN_W) / 2;
    Color fill = app->config.eq_on ? COLOR_ACCENT : COLOR_FAINT;
    gfxFillRect(first_x, zero_y, EQ_BANDS * EQ_COLUMN_W, 1, COLOR_SURFACE_HI);  // The 0 dB line.
    for (int b = 0; b < EQ_BANDS; b++) {
        int cx = first_x + b * EQ_COLUMN_W + EQ_COLUMN_W / 2;
        bool selected = b == app->eq_band;
        if (selected)
            gfxFillRoundRect(cx - 34, top - 52, 68, EQ_SLIDER_H + 110, 12, COLOR_SURFACE_HI);
        gfxFillRoundRect(cx - 3, top, 6, EQ_SLIDER_H, 3, COLOR_BG);
        int knob_y = zero_y - gains[b] * (EQ_SLIDER_H / 2) / EQ_GAIN_MAX;
        int bar_top = knob_y < zero_y ? knob_y : zero_y, bar_h = knob_y < zero_y ? zero_y - knob_y : knob_y - zero_y;
        gfxFillRoundRect(cx - 3, bar_top, 6, bar_h + 1, 3, fill);
        gfxFillCircle(cx, knob_y, selected ? 12 : 10, selected ? COLOR_TEXT : fill);

        char value[8];
        snprintf(value, sizeof(value), "%+d", gains[b]);
        fontDraw(FONT_BOLD, 16, cx - fontTextWidth(FONT_BOLD, 16, value) / 2, top - 40, value,
                 selected ? COLOR_TEXT : COLOR_DIM);
        const char *label = eqBandLabel(b);
        fontDraw(FONT_REGULAR, 16, cx - fontTextWidth(FONT_REGULAR, 16, label) / 2, top + EQ_SLIDER_H + 18, label,
                 COLOR_DIM);
        uiHitAdd(cx - EQ_COLUMN_W / 2, top, EQ_COLUMN_W, EQ_SLIDER_H, HIT_EQ_BAND, b);
    }
    fontDraw(FONT_REGULAR, 16, x + PANEL_PAD, y + EQ_H - 42, "Hz  ·  +12 to -12 dB", COLOR_FAINT);
}

#define ABOUT_W 700
#define ABOUT_H 470

static void drawAbout(const App *app)
{
    static const char *const LINES[] = {
        "A music player for Switch 2 homebrew on kaiHEN.",
        "Plays MP3, FLAC, Ogg Vorbis and WAV from your SD card, with",
        "playlists, an equalizer, ReplayGain and album art.",
        "",
        "Free and open source under the MIT license.",
        "Not affiliated with or endorsed by Nintendo.",
    };
    int x = (GFX_WIDTH - ABOUT_W) / 2, y = (GFX_HEIGHT - ABOUT_H) / 2;
    dimScreen();
    panel(x, y, ABOUT_W, ABOUT_H);
    // A big note in the accent color, beside the name and version.
    gfxFillRoundRect(x + PANEL_PAD, y + 32, 84, 84, 20, COLOR_SURFACE_HI);
    uiDrawNoteIcon(x + PANEL_PAD + 18, y + 50, 48, COLOR_ACCENT);
    int text_x = x + PANEL_PAD + 108;
    fontDraw(FONT_BOLD, 34, text_x, y + 36, "KaiPlayer", COLOR_TEXT);
    fontDraw(FONT_REGULAR, 20, text_x, y + 84, "Version " KAIPLAYER_VERSION, COLOR_DIM);

    int line_y = y + 150;
    fontDraw(FONT_REGULAR, 18, x + PANEL_PAD, line_y, "Made by", COLOR_DIM);
    fontDraw(FONT_BOLD, 26, x + PANEL_PAD, line_y + 24, "Mizuiro", COLOR_ACCENT);
    line_y += 82;
    for (size_t i = 0; i < sizeof(LINES) / sizeof(LINES[0]); i++, line_y += 28)
        fontDraw(FONT_REGULAR, 19, x + PANEL_PAD, line_y, LINES[i], COLOR_TEXT);
    fontDraw(FONT_BOLD, 18, x + PANEL_PAD, y + ABOUT_H - 46, "github.com/mizuiroame/kaiplayer", COLOR_ACCENT);
    if (app->sys.has_battery) {  // Small, for bug reports about charging.
        char power[96];
        snprintf(power, sizeof(power), "Power: %s", app->sys.power_raw);
        fontDraw(FONT_REGULAR, 14, x + ABOUT_W - PANEL_PAD - fontTextWidth(FONT_REGULAR, 14, power), y + ABOUT_H - 42,
                 power, COLOR_FAINT);
    }
}

void uiDrawOverlay(const App *app, uint64_t now_ns)
{
    if (app->keyboard_open)
        drawKeyboard(app, now_ns);
    else if (app->menu.open)
        drawMenu(&app->menu);
    else if (app->eq_open)
        drawEqualizer(app);
    else if (app->about_open)
        drawAbout(app);
    else if (app->settings_open)
        drawSettings(app, now_ns);
}
