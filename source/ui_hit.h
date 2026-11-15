// Touch targets. The UI adds a region for each control as it draws it; a
// tap is matched against the regions of the last frame, topmost first, so
// touch areas always match what is on screen.
#pragma once

#include <stdbool.h>

#define HIT_MAX_REGIONS 512

typedef enum {
    HIT_TAB,           // arg: View.
    HIT_MENU_BUTTON,
    HIT_LIST,          // The scrolling list area, for drags.
    HIT_ROW,           // arg: index in the current list.
    HIT_MINI_PLAYER,
    HIT_SEEK,          // The progress bar: the tap's x gives the position.
    HIT_PREV,
    HIT_PLAY,
    HIT_NEXT,
    HIT_SHUFFLE,
    HIT_REPEAT,
    HIT_BACKDROP,      // Outside an overlay.
    HIT_MENU_ITEM,     // arg: item index.
    HIT_KEY,           // arg: row * 16 + col on the keyboard.
    HIT_SETTING,       // arg: setting row.
} HitKind;

typedef struct {
    int x, y, w, h;
    HitKind kind;
    int arg;
} HitRegion;

void uiHitReset(void);
void uiHitAdd(int x, int y, int w, int h, HitKind kind, int arg);
// The topmost region at (x, y), or NULL.
const HitRegion *uiHitFind(int x, int y);
