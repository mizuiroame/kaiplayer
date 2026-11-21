#include "keyboard.h"

#include <stdio.h>
#include <string.h>

#include "lang.h"

static const char *const LOWER[KEYBOARD_CHAR_ROWS] = {
    "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm-_.",
};
static const char *const UPPER[KEYBOARD_CHAR_ROWS] = {
    "!&()+,1234", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-_.",
};
static const char *const ACTION_LABELS[KEYBOARD_ACTIONS] = {N_("Space"), N_("Delete"), N_("Shift"), N_("Done")};

void keyboardInit(Keyboard *k, const char *initial)
{
    memset(k, 0, sizeof(*k));
    snprintf(k->text, sizeof(k->text), "%s", initial ? initial : "");
    k->row = 1;
}

static int actionForColumn(int col)
{
    return col * KEYBOARD_ACTIONS / KEYBOARD_COLS;
}

static int columnForAction(int action)
{
    return action * KEYBOARD_COLS / KEYBOARD_ACTIONS + 1;
}

void keyboardMove(Keyboard *k, int dx, int dy)
{
    if (dy != 0) {
        int row = (k->row + dy + KEYBOARD_ROWS) % KEYBOARD_ROWS;
        bool leaving_actions = k->row == KEYBOARD_CHAR_ROWS;
        bool entering_actions = row == KEYBOARD_CHAR_ROWS;
        if (leaving_actions && !entering_actions)
            k->col = columnForAction(k->col);
        else if (entering_actions && !leaving_actions)
            k->col = actionForColumn(k->col);
        k->row = row;
    }
    if (dx != 0) {
        int width = k->row == KEYBOARD_CHAR_ROWS ? KEYBOARD_ACTIONS : KEYBOARD_COLS;
        k->col = (k->col + dx + width) % width;
    }
}

char keyboardCharAt(const Keyboard *k, int row, int col)
{
    if (row < 0 || row >= KEYBOARD_CHAR_ROWS || col < 0 || col >= KEYBOARD_COLS)
        return '\0';
    return (k->shift ? UPPER : LOWER)[row][col];
}

const char *keyboardActionLabel(KeyboardAction action)
{
    return T(ACTION_LABELS[action]);
}

void keyboardType(Keyboard *k, char c)
{
    size_t n = strlen(k->text);
    if (n < KEYBOARD_MAX) {
        k->text[n] = c;
        k->text[n + 1] = '\0';
    }
}

void keyboardBackspace(Keyboard *k)
{
    size_t n = strlen(k->text);
    if (n > 0)
        k->text[n - 1] = '\0';
}

void keyboardToggleShift(Keyboard *k)
{
    k->shift = !k->shift;
}

KeyboardResult keyboardPress(Keyboard *k)
{
    if (k->row < KEYBOARD_CHAR_ROWS) {
        keyboardType(k, keyboardCharAt(k, k->row, k->col));
        return KEYBOARD_EDITING;
    }
    switch ((KeyboardAction)k->col) {
    case KEY_ACTION_SPACE: keyboardType(k, ' '); break;
    case KEY_ACTION_DELETE: keyboardBackspace(k); break;
    case KEY_ACTION_SHIFT: keyboardToggleShift(k); break;
    case KEY_ACTION_DONE: return KEYBOARD_DONE;
    }
    return KEYBOARD_EDITING;
}
