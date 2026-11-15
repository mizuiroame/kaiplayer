// The + menu and the on-screen keyboard, drawn over the current tab.
#include <stdio.h>

#include "ui_common.h"

#define DIM_ALPHA 160
#define MENU_W 640
#define MENU_ROW_H 48
#define MENU_VISIBLE 9
#define KEY_W 76
#define KEY_H 56
#define KEY_GAP 8
#define KEYS_W (KEYBOARD_COLS * KEY_W + (KEYBOARD_COLS - 1) * KEY_GAP)
#define PANEL_PAD 32

static void dimScreen(void)
{
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, COLOR_SHADOW, DIM_ALPHA);
}

static int firstVisibleItem(int cursor, int count)
{
    if (count <= MENU_VISIBLE || cursor < MENU_VISIBLE / 2)
        return 0;
    int first = cursor - MENU_VISIBLE / 2;
    return first + MENU_VISIBLE > count ? count - MENU_VISIBLE : first;
}

static void drawMenu(const Menu *m)
{
    int rows = m->count < MENU_VISIBLE ? m->count : MENU_VISIBLE;
    int h = 76 + rows * MENU_ROW_H + 20;
    int x = (GFX_WIDTH - MENU_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    gfxFillRoundRect(x, y, MENU_W, h, 16, COLOR_SURFACE);
    fontDrawClipped(FONT_BOLD, 22, x + PANEL_PAD, y + 24, MENU_W - 2 * PANEL_PAD, m->title, COLOR_TEXT);

    int first = firstVisibleItem(m->cursor, m->count);
    for (int i = first; i < m->count && i < first + rows; i++) {
        int row_y = y + 76 + (i - first) * MENU_ROW_H;
        bool selected = i == m->cursor;
        if (selected) {
            gfxFillRoundRect(x + 16, row_y, MENU_W - 32, MENU_ROW_H - 6, 10, COLOR_SURFACE_HI);
            gfxFillRect(x + 16, row_y + 10, 3, MENU_ROW_H - 26, COLOR_ACCENT);
        }
        fontDrawClipped(selected ? FONT_BOLD : FONT_REGULAR, 21, x + PANEL_PAD, row_y + 9,
                        MENU_W - 2 * PANEL_PAD, m->items[i].label, COLOR_TEXT);
    }
    if (m->count > rows) {
        char more[32];
        snprintf(more, sizeof(more), "%d / %d", m->cursor + 1, m->count);
        fontDraw(FONT_REGULAR, 16, x + MENU_W - PANEL_PAD - fontTextWidth(FONT_REGULAR, 16, more), y + 28,
                 more, COLOR_DIM);
    }
}

static void drawKey(int x, int y, int w, const char *label, bool selected, bool lit)
{
    gfxFillRoundRect(x, y, w, KEY_H, 10, selected ? COLOR_ACCENT : COLOR_SURFACE_HI);
    Color text = selected ? COLOR_BG : lit ? COLOR_ACCENT : COLOR_TEXT;
    int label_w = fontTextWidth(FONT_BOLD, 22, label);
    fontDraw(FONT_BOLD, 22, x + (w - label_w) / 2, y + 13, label, text);
}

static void drawKeyboard(const App *app, uint64_t now_ns)
{
    const Keyboard *k = &app->keyboard;
    int w = KEYS_W + 2 * PANEL_PAD;
    int h = 150 + KEYBOARD_ROWS * (KEY_H + KEY_GAP) + 52;
    int x = (GFX_WIDTH - w) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    gfxFillRoundRect(x, y, w, h, 16, COLOR_SURFACE);
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
                    k->row == r && k->col == c, false);
        }
    }
    int action_w = (KEYS_W - (KEYBOARD_ACTIONS - 1) * KEY_GAP) / KEYBOARD_ACTIONS;
    int action_y = keys_y + KEYBOARD_CHAR_ROWS * (KEY_H + KEY_GAP);
    for (int a = 0; a < KEYBOARD_ACTIONS; a++) {
        drawKey(x + PANEL_PAD + a * (action_w + KEY_GAP), action_y, action_w,
                keyboardActionLabel((KeyboardAction)a), k->row == KEYBOARD_CHAR_ROWS && k->col == a,
                a == KEY_ACTION_SHIFT && k->shift);
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
}
