// The + menu, Settings and the on-screen keyboard, drawn over the current tab.
#include <stdio.h>
#include <string.h>

#include "lang.h"
#include "ui_brand.h"
#include "nowstyle.h"
#include "settings.h"
#include "theme.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "ui_list.h"

#ifndef KAIPLAYER_VERSION
#define KAIPLAYER_VERSION "dev"
#endif

#define MENU_W 640
#define MENU_ROW_H 48
#define MENU_VISIBLE 9
#define MENU_DIVIDER_H 12
#define SETTINGS_W 760
#define SETTINGS_ROW_H 48
#define KEY_W 76
#define KEY_H 56
#define KEY_GAP 8
#define KEYS_W (KEYBOARD_COLS * KEY_W + (KEYBOARD_COLS - 1) * KEY_GAP)
#define PANEL_PAD 32
#define NS_PER_MINUTE 60000000000ull
#define NOTE_PX 16
#define NOTE_LINE_H 22
#define NOTE_LINES 2

// Darkens the screen; a tap there closes the overlay.
// uiDraw has already dimmed what's behind (see uiOverlayOpen); taps on it close the overlay.
static void dimScreen(void)
{
    uiHitAdd(0, 0, GFX_WIDTH, GFX_HEIGHT, HIT_BACKDROP, 0);
}

static void panel(int x, int y, int w, int h)
{
    gfxFillRoundRect(x, y, w, h, 16, COLOR_SURFACE);
    uiHitAdd(x, y, w, h, HIT_PANEL, 0);  // Taps on the panel itself do nothing.
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

// The + menu's line between this screen's actions and the app's: room above
// the item after it, the line in the middle of the gap.
static bool dividerShown(const Menu *m, int first, int rows)
{
    return m->divider > first && m->divider < first + rows && m->divider < m->count;
}

static void drawMenu(const Menu *m)
{
    int rows = m->count < MENU_VISIBLE ? m->count : MENU_VISIBLE;
    int first = firstVisibleItem(m->cursor, m->count);
    bool divider = dividerShown(m, first, rows);
    int h = 76 + rows * MENU_ROW_H + (divider ? MENU_DIVIDER_H : 0) + 20;
    int x = (GFX_WIDTH - MENU_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, MENU_W, h);
    if (m->kind == MENU_KIND_MAIN) {  // The app's name in its color, its version beside it (as KaiVideo's).
        int title_w = fontDraw(FONT_BOLD, 22, x + PANEL_PAD, y + 24, m->title, COLOR_ACCENT);
        fontDraw(FONT_REGULAR, 15, x + PANEL_PAD + title_w + 12, y + 31, KAIPLAYER_VERSION, COLOR_FAINT);
    } else {
        fontDrawClipped(FONT_BOLD, 22, x + PANEL_PAD, y + 24, MENU_W - 2 * PANEL_PAD, m->title, COLOR_TEXT);
    }

    for (int i = first; i < m->count && i < first + rows; i++) {
        int row_y = y + 76 + (i - first) * MENU_ROW_H;
        if (divider && i >= m->divider) {
            row_y += MENU_DIVIDER_H;
            if (i == m->divider)  // Inset like the rows, in the gap between them.
                gfxFillRect(x + 16, row_y - (MENU_DIVIDER_H + 6) / 2, MENU_W - 32, 1, COLOR_SURFACE_HI);
        }
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
            snprintf(out, size, "%s", T("Off"));
        } else {
            unsigned left = (unsigned)((app->sleep_deadline_ns - now_ns + NS_PER_MINUTE - 1) / NS_PER_MINUTE);
            snprintf(out, size, T("%d min  (%u left)"), app->sleep_minutes, left);
        }
        break;
    case SETTING_RESUME: {
        static const char *const MODES[RESUME_MODE_COUNT] = {N_("Off"), N_("Paused"), N_("Keep playing")};
        snprintf(out, size, "%s", T(MODES[app->config.resume % RESUME_MODE_COUNT]));
        break;
    }
    case SETTING_LIBRARY:
        snprintf(out, size, "%.60s", app->config.library[0] ? (utilFileName(app->config.library)[0]
                                                                  ? utilFileName(app->config.library)
                                                                  : app->config.library)
                                                           : T("Not chosen"));
        break;
    case SETTING_CLOCK: snprintf(out, size, "%s", app->config.clock_24h ? T("24-hour") : T("12-hour")); break;
    case SETTING_LANGUAGE:
        snprintf(out, size, "%s", app->config.language_name[0] ? app->config.language_name : "English");
        break;
    case SETTING_EQUALIZER:
        snprintf(out, size, "%s", app->config.eq_on ? eqPresetName((EqPreset)app->config.eq_preset) : T("Off"));
        break;
    case SETTING_REPLAYGAIN: {
        static const char *const MODES[REPLAYGAIN_MODE_COUNT] = {N_("Off"), N_("Track"), N_("Album"), N_("Auto")};
        snprintf(out, size, "%s", T(MODES[app->config.replaygain % REPLAYGAIN_MODE_COUNT]));
        break;
    }
    case SETTING_ACCENT: snprintf(out, size, "%s", themeAccentName(app->config.accent)); break;
    case SETTING_NET_COLOR:
    case SETTING_SRV_COLOR: {
        int choice = row == SETTING_NET_COLOR ? app->config.network_color : app->config.server_color;
        snprintf(out, size, "%s", choice ? themeAccentName(choice - 1) : T("Its own"));
        break;
    }
    case SETTING_NOW_STYLE: snprintf(out, size, "%s", nowStyleName(app->config.now_style)); break;
    case SETTING_BACKGROUND:
        snprintf(out, size, "%.40s", app->config.background[0] ? app->config.background : T("Cover colors"));
        break;
    case SETTING_BACKGROUND_BLUR: snprintf(out, size, "%s", backgroundBlurName(app->config.background_blur)); break;
    case SETTING_OPEN_SERVER:
    case SETTING_SRV_ADDRESS:
    case SETTING_SRV_ACCOUNT:
    case SETTING_SRV_API_KEY:
    case SETTING_SRV_CERTIFICATE: {
        char address[128], user[128];
        bool check;
        appServerShown(address, sizeof(address), user, sizeof(user), &check);
        if (row == SETTING_SRV_CERTIFICATE)
            snprintf(out, size, "%s", check ? T("On") : T("Off: any certificate"));
        else if (row == SETTING_SRV_ACCOUNT && appServerUsesKey())
            snprintf(out, size, "%s", T("The API key is used"));
        else if (row == SETTING_SRV_ACCOUNT)
            snprintf(out, size, "%.40s", user[0] ? user : T("Not signed in"));
        else if (row == SETTING_SRV_API_KEY && appServerUsesKey())
            snprintf(out, size, "%.40s", user[0] ? user : T("Set"));
        else if (row == SETTING_SRV_API_KEY)
            snprintf(out, size, "%s", T("Not set"));
        else
            snprintf(out, size, "%.40s", address[0] ? address : T("Not set"));
        break;
    }
    case SETTING_CREDITS: out[0] = '\0'; break;  // Opens the panel: just the A button when chosen.
    case SETTING_UPDATE_AUTO:
    case SETTING_UPDATE_NOW: appUpdatesValue(app, row, out, size); break;
    case SETTING_SRV_TEST:
    case SETTING_SRV_FORGET: out[0] = '\0'; break;
    case SETTING_OPEN_LIBRARY:
    case SETTING_OPEN_PLAYBACK:
    case SETTING_OPEN_DISPLAY: out[0] = '\0'; break;  // Just the arrow: these open a page.
    case SETTING_RESCAN:                               // Just the A button when chosen.
    case SETTING_NET_TEST:
    case SETTING_NET_FORGET: out[0] = '\0'; break;
    case SETTING_NET_REMEMBER:
    case SETTING_NET_UNSIGNED:
    case SETTING_NET_CERTIFICATE: {
        bool remember, allow_unsigned, smb, check_certificate;
        appNetworkChoices(&remember, &allow_unsigned, &smb, &check_certificate);
        if (row == SETTING_NET_REMEMBER)
            snprintf(out, size, "%s", remember ? T("On") : T("Off: asked when the folder is used"));
        else if (row == SETTING_NET_CERTIFICATE)  // For an https folder's connections (as KaiVideo's).
            snprintf(out, size, "%s", check_certificate ? T("On") : T("Off: any certificate"));
        else if (!smb)
            snprintf(out, size, "%s", T("Only for shared folders (SMB)"));
        else
            snprintf(out, size, "%s", allow_unsigned ? T("On: not signed") : T("Off"));
        break;
    }
    case SETTING_OPEN_NETWORK:
    case SETTING_NET_ADDRESS:
    case SETTING_NET_ACCOUNT: {
        char address[128], user[128];
        appNetworkShown(address, sizeof(address), user, sizeof(user));
        if (row == SETTING_NET_ACCOUNT)
            snprintf(out, size, "%.40s", user[0] ? user : T("None"));
        else
            snprintf(out, size, "%.40s", address[0] ? address : T("Not set"));
        break;
    }
    case SETTING_SCREEN_OFF:
        if (app->config.screen_off_minutes)
            snprintf(out, size, T("After %d min idle"), app->config.screen_off_minutes);
        else
            snprintf(out, size, "%s", T("Never"));
        break;
    default: snprintf(out, size, "KaiPlayer %s", KAIPLAYER_VERSION); break;
    }
}

// The end of the line of text that fits in width: at a space if there's one, else between characters.
static const char *lineEnd(const char *text, int width)
{
    const char *at = text, *space = NULL;
    char line[512];
    while (*at) {
        const char *next = at;
        utilNextCodePoint(&next);
        size_t length = (size_t)(next - text);
        if (length >= sizeof(line))
            break;
        memcpy(line, text, length);
        line[length] = '\0';
        if (fontTextWidth(FONT_REGULAR, NOTE_PX, line) > width)
            return space ? space : at > text ? at : next;
        if (*at == ' ')
            space = at;
        at = next;
    }
    return at;
}

// A page's note, on up to two lines (drawn if draw). How many it takes.
static int drawNote(int x, int y, int width, const char *text, bool draw)
{
    int lines = 0;
    while (*text && lines < NOTE_LINES) {
        const char *end = lines + 1 < NOTE_LINES ? lineEnd(text, width) : text + strlen(text);
        char line[512];
        snprintf(line, sizeof(line), "%.*s", (int)(end - text), text);
        if (draw)
            fontDrawClipped(FONT_REGULAR, NOTE_PX, x, y + lines * NOTE_LINE_H, width, line, COLOR_DIM);
        lines++;
        text = *end == ' ' ? end + 1 : end;
    }
    return lines;
}

#define SOURCE_DOT_SIZE 12
#define SOURCE_DOT_ROOM 20  // The dot and the gap after it.

static void sourceDot(int x, int middle, Color color)
{
    gfxFillRoundRect(x, middle - SOURCE_DOT_SIZE / 2, SOURCE_DOT_SIZE, SOURCE_DOT_SIZE, SOURCE_DOT_SIZE / 2, color);
}

#define SOURCE_BAND_H 5  // A source's page: its color along the top, as KaiVideo's.

static bool isSourcePage(int page)
{
    return page == SETTINGS_PAGE_NETWORK || page == SETTINGS_PAGE_SERVER;
}

static void drawSettingsPanel(const App *app, uint64_t now_ns)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows(app->settings_page, rows);
    const char *note = settingsPageNote(app->settings_page);
    int note_lines = note ? drawNote(0, 0, SETTINGS_W - 2 * PANEL_PAD, note, false) : 1;
    int h = 76 + count * SETTINGS_ROW_H + 60 + (note_lines - 1) * NOTE_LINE_H;
    int x = (GFX_WIDTH - SETTINGS_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, SETTINGS_W, h);
    if (isSourcePage(app->settings_page))
        gfxFillRoundRect(x + 16, y, SETTINGS_W - 32, SOURCE_BAND_H, SOURCE_BAND_H / 2, COLOR_ACCENT);
    int title_x = x + PANEL_PAD;
    if (app->settings_page != SETTINGS_PAGE_MAIN) {  // A page: back to the main one.
        title_x += uiDrawBackButtonHit(x + PANEL_PAD - 4, y + 18, false, HIT_OVERLAY_BACK) + 12;
    }
    if (app->settings_page == SETTINGS_PAGE_NETWORK || app->settings_page == SETTINGS_PAGE_SERVER) {  // Its color.
        sourceDot(title_x, y + 37, app->settings_page == SETTINGS_PAGE_NETWORK ? COLOR_NETWORK : COLOR_SERVER);
        title_x += SOURCE_DOT_ROOM;
    }
    fontDraw(FONT_BOLD, 24, title_x, y + 22, settingsPageTitle(app->settings_page), COLOR_TEXT);

    char value[128];
    for (int n = 0; n < count; n++) {
        int i = (int)rows[n];
        int row_y = y + 76 + n * SETTINGS_ROW_H;
        bool selected = app->settings_cursor == rows[n];
        bool editable = settingsRowEditable((SettingRow)i);
        bool opens = settingsRowPage((SettingRow)i) != SETTINGS_PAGE_MAIN;
        if (selected)
            drawRowHighlight(x + 16, row_y, SETTINGS_W - 32, SETTINGS_ROW_H - 6);
        int label_x = x + PANEL_PAD;
        if (i == SETTING_OPEN_NETWORK || i == SETTING_OPEN_SERVER) {  // A source: its color, as KaiVideo's.
            sourceDot(label_x, row_y + 22, i == SETTING_OPEN_NETWORK ? COLOR_NETWORK : COLOR_SERVER);
            label_x += SOURCE_DOT_ROOM;
        }
        fontDraw(selected ? FONT_BOLD : FONT_REGULAR, 21, label_x, row_y + 8, settingsRowLabel((SettingRow)i), COLOR_TEXT);
        settingValue(app, (SettingRow)i, now_ns, value, sizeof(value));
        int value_w = fontTextWidth(FONT_BOLD, 20, value);
        int value_x = x + SETTINGS_W - PANEL_PAD - value_w - (editable || opens ? 24 : 0);
        if (value[0])
            fontDraw(FONT_BOLD, 20, value_x, row_y + 9, value, editable ? COLOR_ACCENT : COLOR_DIM);
        if (opens) {  // A page: an arrow into it.
            gfxFillTriangleRight(x + SETTINGS_W - PANEL_PAD - 12, row_y + 13, 10, 16, selected ? COLOR_TEXT : COLOR_DIM);
        } else if (editable) {
            gfxFillTriangleLeft(value_x - 22, row_y + 13, 10, 16, COLOR_DIM);
            gfxFillTriangleRight(x + SETTINGS_W - PANEL_PAD - 12, row_y + 13, 10, 16, COLOR_DIM);
        } else if (selected) {  // A opens or does it: About, the music folder, Update the library.
            uiDrawButton(value_x - (value[0] ? 34 : 22), (float)row_y + 21.0f, "A", COLOR_SURFACE, COLOR_TEXT);
        }
        uiHitAdd(x + 16, row_y, SETTINGS_W - 32, SETTINGS_ROW_H - 6, HIT_SETTING, i);
    }
    if (note)  // What the page is for, instead of the project's line.
        drawNote(x + PANEL_PAD, y + h - 40 - (note_lines - 1) * NOTE_LINE_H, SETTINGS_W - 2 * PANEL_PAD, note, true);
    else
        fontDrawClipped(FONT_REGULAR, 16, x + PANEL_PAD, y + h - 40, SETTINGS_W - 2 * PANEL_PAD,
                        T("Open source (MIT)  ·  github.com/mizuiroame/kaiplayer"), COLOR_FAINT);
}

// A source's page in its color (the accent's, for as long as it's drawn).
static void drawSettings(const App *app, uint64_t now_ns)
{
    Color accent = g_ui_accent;
    if (isSourcePage(app->settings_page))
        g_ui_accent = app->settings_page == SETTINGS_PAGE_NETWORK ? COLOR_NETWORK : COLOR_SERVER;
    drawSettingsPanel(app, now_ns);
    g_ui_accent = accent;
}

static void drawKey(int x, int y, int w, const char *label, bool selected, bool lit, int hit_arg)
{
    gfxFillRoundRect(x, y, w, KEY_H, 10, selected ? COLOR_ACCENT : COLOR_SURFACE_HI);
    Color text = selected ? COLOR_BG : lit ? COLOR_ACCENT : COLOR_TEXT;
    int label_w = fontTextWidth(FONT_BOLD, 22, label);
    fontDraw(FONT_BOLD, 22, x + (w - label_w) / 2, y + 13, label, text);
    uiHitAdd(x, y, w, KEY_H, HIT_KEY, hit_arg);
}

static void drawKeyboard(const App *app)
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
    // A message such as an invalid name is said in the notice bubble, over the keyboard.
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
    fontDraw(FONT_BOLD, 24, x + PANEL_PAD, y + 22, T("Equalizer"), COLOR_TEXT);

    // Preset and on/off pills, top right.
    char preset[128], toggle[64];
    snprintf(preset, sizeof(preset), "X  %s", eqPresetName((EqPreset)app->config.eq_preset));
    snprintf(toggle, sizeof(toggle), "Y  %s", app->config.eq_on ? T("On") : T("Off"));
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
    fontDraw(FONT_REGULAR, 16, x + PANEL_PAD, y + EQ_H - 42, T("Hz  ·  +12 to -12 dB"), COLOR_FAINT);
}

#define PICKER_W 760
#define PICKER_ROWS 8
#define PICKER_ROW_H 44

static void drawFolderPicker(const App *app)
{
    const Browser *b = &app->picker;
    size_t folders = appPickerFolderCount(app);
    int h = 150 + PICKER_ROWS * PICKER_ROW_H + 70;
    int x = (GFX_WIDTH - PICKER_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, PICKER_W, h);
    fontDraw(FONT_BOLD, 24, x + PANEL_PAD, y + 22,
             T("Choose your music folder"), COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 17, x + PANEL_PAD, y + 62, PICKER_W - 2 * PANEL_PAD,
                    T("KaiPlayer only shows this folder and the ones inside it."), COLOR_DIM);
    fontDrawClipped(FONT_BOLD, 18, x + PANEL_PAD, y + 100, PICKER_W - 2 * PANEL_PAD, b->path, COLOR_ACCENT);

    int list_y = y + 136;
    if (folders == 0)
        fontDraw(FONT_REGULAR, 19, x + PANEL_PAD, list_y + 10, T("No folders here."), COLOR_DIM);
    size_t first = b->cursor >= PICKER_ROWS ? b->cursor - PICKER_ROWS + 1 : 0;
    for (size_t i = first; i < folders && i < first + PICKER_ROWS; i++) {
        int row_y = list_y + (int)(i - first) * PICKER_ROW_H;
        if (i == b->cursor)
            drawRowHighlight(x + 16, row_y, PICKER_W - 32, PICKER_ROW_H - 4);
        uiDrawFolderIcon(x + PANEL_PAD, row_y + 12, COLOR_FOLDER);
        fontDrawClipped(i == b->cursor ? FONT_BOLD : FONT_REGULAR, 20, x + PANEL_PAD + 40, row_y + 8,
                        PICKER_W - 2 * PANEL_PAD - 40, b->entries[i].name, COLOR_TEXT);
        uiHitAdd(x + 16, row_y, PICKER_W - 32, PICKER_ROW_H - 4, HIT_PICKER_ROW, (int)i);
    }

    // "Use this folder", bottom right.
    const char *const USE = T("Use this folder");
    int use_w = uiIconPillWidth("X", USE), use_x = x + PICKER_W - PANEL_PAD - use_w, use_y = y + h - 54;
    uiDrawIconPill(use_x, use_y, "X", PILL_ICON_CHECK, USE, true);
    uiHitAdd(use_x, use_y - 8, use_w, 46, HIT_PICKER_USE, 0);
}

#define WELCOME_W 760
#define WELCOME_ROW_H 76

// The first run's question. A tap beside it does nothing (app_touch.c): a stray touch shouldn't answer it.
static void drawWelcome(const App *app)
{
    static const char *const TITLES[WELCOME_ROW_COUNT] = {
        N_("A folder on the SD card"),
        N_("A network folder"),
        N_("A Navidrome server"),
        N_("Later"),
    };
    static const char *const NOTES[WELCOME_ROW_COUNT] = {
        N_("Choose the folder that has your music."),
        N_("SMB or WebDAV, on a PC or a NAS."),
        N_("Sign in with its address, user and password."),
        N_("Start without music; set it up in + > Settings."),
    };
    int h = 116 + WELCOME_ROW_COUNT * WELCOME_ROW_H + 18;
    int x = (GFX_WIDTH - WELCOME_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, WELCOME_W, h);
    fontDraw(FONT_BOLD, 26, x + PANEL_PAD, y + 24, T("Welcome! Where is your music?"), COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 17, x + PANEL_PAD, y + 66, WELCOME_W - 2 * PANEL_PAD,
                    T("You can change it any time in + > Settings."), COLOR_DIM);
    int list_y = y + 116;
    for (int i = 0; i < WELCOME_ROW_COUNT; i++) {
        int row_y = list_y + i * WELCOME_ROW_H;
        bool on = i == app->welcome_cursor;
        if (on)
            drawRowHighlight(x + 16, row_y, WELCOME_W - 32, WELCOME_ROW_H - 6);
        fontDrawClipped(on ? FONT_BOLD : FONT_REGULAR, 21, x + PANEL_PAD + 8, row_y + 10,
                        WELCOME_W - 2 * PANEL_PAD - 8, T(TITLES[i]), i == WELCOME_LATER ? COLOR_DIM : COLOR_TEXT);
        fontDrawClipped(FONT_REGULAR, 16, x + PANEL_PAD + 8, row_y + 40, WELCOME_W - 2 * PANEL_PAD - 8, T(NOTES[i]),
                        COLOR_DIM);
        uiHitAdd(x + 16, row_y, WELCOME_W - 32, WELCOME_ROW_H - 6, HIT_WELCOME_ROW, i);
    }
}

#define ABOUT_W 700
#define ABOUT_H 498

static void drawAbout(void)
{
    // One text each, drawn a line at a time: a translation may break its lines elsewhere.
    static const char *const LINES[] = {
        N_("A music player for Switch 2 homebrew on kaiHEN."),
        N_("Plays MP3, FLAC, AAC, ALAC, Opus, Ogg Vorbis, WAV and DSD, with\n"
           "playlists, an equalizer, ReplayGain and album art."),
        N_("From the SD card, a network folder (SMB or WebDAV) or a Navidrome server."),
        "",
        N_("Free and open source under the MIT license; FFmpeg and libsmb2 under the LGPL."),
        N_("Not affiliated with or endorsed by Nintendo."),
    };
    int x = (GFX_WIDTH - ABOUT_W) / 2, y = (GFX_HEIGHT - ABOUT_H) / 2;
    dimScreen();
    panel(x, y, ABOUT_W, ABOUT_H);
    uiDrawAppIcon(x + PANEL_PAD, y + 32);  // KaiPlayer's icon, beside the name and version.
    int text_x = x + PANEL_PAD + 108;
    fontDraw(FONT_BOLD, 34, text_x, y + 36, "KaiPlayer", COLOR_TEXT);
    char version[64];
    snprintf(version, sizeof(version), T("Version %s"), KAIPLAYER_VERSION);
    fontDraw(FONT_REGULAR, 20, text_x, y + 84, version, COLOR_DIM);

    int line_y = y + 150;
    uiDrawBrandAt(x + ABOUT_W - PANEL_PAD, y + ABOUT_H - 24);  // Mizuiro's mark, at the bottom right.
    for (size_t i = 0; i < sizeof(LINES) / sizeof(LINES[0]); i++) {
        const char *text = LINES[i][0] ? T(LINES[i]) : "";
        do {  // Each of its lines.
            const char *end = strchr(text, '\n');
            char line[256];
            snprintf(line, sizeof(line), "%.*s", end ? (int)(end - text) : (int)strlen(text), text);
            fontDraw(FONT_REGULAR, 19, x + PANEL_PAD, line_y, line, COLOR_TEXT);
            line_y += 28;
            text = end ? end + 1 : "";
        } while (*text);
    }
    fontDraw(FONT_BOLD, 18, x + PANEL_PAD, y + ABOUT_H - 46, "github.com/mizuiroame/kaiplayer", COLOR_ACCENT);
}

bool uiOverlayOpen(const App *app)
{
    return app->keyboard_open || app->menu.open || app->picker_open || app->eq_open || app->about_open ||
           app->settings_open || app->welcome_open;
}

void uiDrawOverlay(const App *app, uint64_t now_ns)
{
    if (app->keyboard_open)
        drawKeyboard(app);
    else if (app->menu.open)
        drawMenu(&app->menu);
    else if (app->picker_open)
        drawFolderPicker(app);
    else if (app->welcome_open)
        drawWelcome(app);
    else if (app->eq_open)
        drawEqualizer(app);
    else if (app->about_open)
        drawAbout();
    else if (app->settings_open)
        drawSettings(app, now_ns);
}
