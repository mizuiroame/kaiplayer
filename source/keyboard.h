// An on-screen keyboard for playlist names. It types only characters that
// are safe in file names on the console (Latin letters, digits and a few
// punctuation marks). The UI calls these functions from button presses.
#pragma once

#include <stdbool.h>

#define KEYBOARD_MAX 48
#define KEYBOARD_COLS 10
#define KEYBOARD_CHAR_ROWS 4
#define KEYBOARD_ROWS (KEYBOARD_CHAR_ROWS + 1)  // The last row holds the actions.
#define KEYBOARD_ACTIONS 4

typedef enum {
    KEY_ACTION_SPACE,
    KEY_ACTION_DELETE,
    KEY_ACTION_SHIFT,
    KEY_ACTION_DONE,
} KeyboardAction;

typedef enum {
    KEYBOARD_EDITING,
    KEYBOARD_DONE,
} KeyboardResult;

typedef struct {
    char text[KEYBOARD_MAX + 1];
    int row;    // 0 to KEYBOARD_ROWS - 1.
    int col;    // 0 to KEYBOARD_COLS - 1, or an action on the last row.
    bool shift;
} Keyboard;

void keyboardInit(Keyboard *k, const char *initial);
void keyboardMove(Keyboard *k, int dx, int dy);
// Presses the selected key. Returns KEYBOARD_DONE for the Done key.
KeyboardResult keyboardPress(Keyboard *k);
void keyboardBackspace(Keyboard *k);
void keyboardToggleShift(Keyboard *k);
void keyboardType(Keyboard *k, char c);

// The character at a key of the character rows, with the shift state.
char keyboardCharAt(const Keyboard *k, int row, int col);
const char *keyboardActionLabel(KeyboardAction action);
