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
    HIT_LYRICS,       // Now Playing: shows or hides the lyrics.
    HIT_BACKDROP,      // Outside an overlay.
    HIT_MENU_ITEM,     // arg: item index.
    HIT_KEY,           // arg: row * 16 + col on the keyboard.
    HIT_SETTING,       // arg: setting row.
    HIT_BACK,          // Like B: up a folder, or out of a playlist.
    HIT_ADD,           // "+": add to a playlist. arg: row index, or -1 for the current song.
    HIT_EQ_BAND,       // An equalizer slider: the tap's y gives the gain. arg: band.
    HIT_EQ_PRESET,     // Next equalizer preset.
    HIT_EQ_TOGGLE,     // Equalizer on/off.
    HIT_PICKER_ROW,    // A folder in the music folder picker. arg: index.
    HIT_PICKER_USE,    // "Use this folder".
    HIT_CARD,          // A Home card. arg: row * HOME_ROW_MAX + card.
    HIT_OVERLAY_BACK,  // Like B in an overlay: a Settings page's Back.
    HIT_PANEL,         // An overlay's panel: a tap on it does nothing (not the backdrop's close).
    HIT_SOURCE,        // Library's source: the card's music, the network folder's, or the server's.
    HIT_NETWORK_SCAN,  // A library on the network being read: shows it. arg: 0 the network folder's, 1 the server's.
    HIT_NOTICE,        // The notice bubble at the top, over any layer: a tap sends it away.
} HitKind;

typedef struct {
    int x, y, w, h;
    HitKind kind;
    int arg;
} HitRegion;

void uiHitReset(void);
void uiHitAdd(int x, int y, int w, int h, HitKind kind, int arg);
// Targets added from now on are cut to between top and bottom (a list's place).
void uiHitClip(int top, int bottom);
// The topmost region at (x, y), or NULL.
const HitRegion *uiHitFind(int x, int y);

// True for the targets of an overlay (menu, Settings, keyboard, equalizer,
// picker): only they answer while one is open, and only the others when none is.
bool uiHitIsOverlayTarget(HitKind kind);

// The regions of the last frame, bottom first: for checks.
int uiHitCount(void);
const HitRegion *uiHitAt(int index);
