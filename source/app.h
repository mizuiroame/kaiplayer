// State of the application shared by input handling and drawing, and the
// handlers that act on it (app_*.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "browser.h"
#include "config.h"
#include "keyboard.h"
#include "menu.h"
#include "playlist.h"
#include "settings.h"
#include "sysinfo.h"
#include "touch.h"
#include "tracklist.h"

typedef enum {
    VIEW_BROWSER,
    VIEW_PLAYLISTS,
    VIEW_QUEUE,
    VIEW_NOW_PLAYING,
    VIEW_COUNT,
} View;

// The Playlists tab: the list of playlists, or the songs of an open one.
typedef struct {
    PlaylistIndex index;
    size_t cursor;
    bool open;                          // Showing the songs of `name`.
    char name[PLAYLIST_NAME_MAX + 1];
    TrackList tracks;
    size_t track_cursor;
    bool moving;                        // Up/Down move the selected song.
} PlaylistsTab;

// Menus that the app treats specially. Others are MENU_KIND_OTHER.
enum {
    MENU_KIND_OTHER,
    MENU_KIND_MAIN,  // The + menu.
    MENU_KIND_EXIT,  // "Exit KaiPlayer?"
};

// What the on-screen keyboard is typing a name for.
typedef enum {
    NAME_FOR_NEW_PLAYLIST,
    NAME_FOR_NEW_PLAYLIST_WITH_SONGS,  // Then add `pending` to it.
    NAME_FOR_RENAME,
} NamePurpose;

typedef struct {
    View view;
    Browser browser;
    Config config;
    size_t queue_cursor;
    PlaylistsTab playlists;
    bool audio_ok;
    SysInfo sys;  // Clock and battery, refreshed by the main loop.
    char toast[160];
    uint64_t toast_until_ns;

    Menu menu;              // The + menu and the dialogs it opens.
    bool keyboard_open;
    Keyboard keyboard;
    NamePurpose keyboard_for;
    char keyboard_title[96];
    TrackList pending;      // Songs waiting for a playlist to be picked.
    bool settings_open;
    SettingRow settings_cursor;
    int sleep_option;           // Index into the sleep timer choices; 0 is off.
    uint64_t sleep_deadline_ns; // When the sleep timer fires; 0 if off.
    TouchTracker touch;
    int drag_rows;              // Pixels dragged on a list, not yet a whole row.
    bool quit;
} App;

// Shows a short message above the player bar.
void appToast(App *app, const char *message);

// Tab handlers. pressed holds the buttons pressed this frame (with repeat).
void appHandleBrowser(App *app, uint64_t pressed);
void appHandlePlaylists(App *app, uint64_t pressed);

// Playlists tab helpers.
void appPlaylistsRefresh(App *app);
void appPlaylistsClose(App *app);
// Reloads the open playlist if it is `name`, after songs were added to it.
void appPlaylistsReloadIfOpen(App *app, const char *name);
// Puts the cursor on the playlist called `name`, if it exists.
void appPlaylistsSelect(App *app, const char *name);
void appPlayTracks(App *app, TrackList *tracks, size_t start, bool shuffle);

// The + menu, its dialogs and the keyboard. appOpenMenu builds the menu for
// the current tab; appHandleOverlay returns true while an overlay is open.
void appOpenMenu(App *app);
bool appHandleOverlay(App *app, uint64_t pressed);

// Switches tabs, refreshing what the new tab shows.
void appSetView(App *app, View view);
// Selects the item at index of the current tab's list and, unless
// select_only, opens it as A would.
void appActivateRow(App *app, size_t index, bool select_only);

// Settings page and sleep timer (app_settings.c).
void appOpenSettings(App *app);
void appHandleSettings(App *app, uint64_t pressed);
void appChangeSetting(App *app, SettingRow row, int delta);
// Pauses with a fade-out when the sleep timer runs out.
void appTickSleepTimer(App *app, uint64_t now_ns);

// Touch input (app_touch.c).
void appHandleTouch(App *app, uint64_t now_ns);

// Handles buttons as if pressed this frame: overlays first, then the tab.
// Touch uses it to act like the matching button.
void appDispatchButtons(App *app, uint64_t pressed);

// Resume: restore the last queue at startup and save it while playing.
void appRestoreSession(App *app);
void appSaveSession(App *app, uint64_t now_ns, bool force);
