// A list of choices shown over the current tab: the + menu, the playlist
// picker and confirmations.
#pragma once

#include <stdbool.h>

#define MENU_MAX_ITEMS 520  // The picker lists up to 512 playlists.
#define MENU_LABEL_MAX 256  // Room for a 48-character name in UTF-8 and its words.

typedef struct {
    char label[MENU_LABEL_MAX];
    int action;  // What choosing the item does; defined by the app.
    int arg;     // Extra data, such as a playlist index.
} MenuItem;

typedef struct {
    bool open;
    char title[MENU_LABEL_MAX];
    MenuItem items[MENU_MAX_ITEMS];
    int count;
    int cursor;
    int kind;  // Which menu this is; defined by the app, 0 after menuOpen.
} Menu;

void menuOpen(Menu *m, const char *title);
void menuClose(Menu *m);
// Adds an item. False if the menu is full.
bool menuAdd(Menu *m, const char *label, int action, int arg);
void menuMove(Menu *m, int delta);
const MenuItem *menuSelected(const Menu *m);
