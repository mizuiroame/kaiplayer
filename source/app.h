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

// What the buttons act on in a list: its rows, "< Back", a row's "+", or
// the controls in the player bar below the list.
typedef enum {
    FOCUS_LIST,
    FOCUS_BACK,
    FOCUS_ADD,
    FOCUS_PLAYER,
} Focus;

// The player bar's controls, left to right.
typedef enum {
    BAR_PREV,
    BAR_PLAY,
    BAR_NEXT,
    BAR_SHUFFLE,
    BAR_REPEAT,
    BAR_COUNT,
} BarControl;

// Where the songs for "add to playlist" come from.
typedef enum {
    SRC_BROWSE_ENTRY,   // The selected song or folder in Browse.
    SRC_QUEUE_ENTRY,    // The selected song in Queue.
    SRC_NOW_PLAYING,    // The current song.
    SRC_PLAYLIST_SONG,  // The selected song of the open playlist.
} AddSource;

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
    Focus focus;
    BarControl bar_control;     // Selected player bar control, with FOCUS_PLAYER.
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
    bool eq_open;               // The equalizer editor (over Settings).
    bool about_open;            // About KaiPlayer (over Settings).
    int eq_band;                // Selected band in the editor.
    bool screen_off;            // Screen off while music plays.
    bool touch_wakes;           // The touch that woke the screen: ignore until released.
    uint64_t last_input_ns;     // For turning the screen off when idle.
    int sleep_minutes;          // The sleep timer's length; 0 is off.
    uint64_t sleep_deadline_ns; // When the sleep timer fires; 0 if off.
    TouchTracker touch;
    int drag_rows;              // Pixels dragged on a list, not yet a whole row.
    bool scrubbing;             // Dragging on the progress bar.
    double scrub_fraction;      // Where the finger is on the bar, 0 to 1.
    bool quit;
} App;

// Shows a short message above the player bar.
void appToast(App *app, const char *message);

// Moves the focus between a list, its "< Back" (Up from the first row), its
// rows' "+" (Right; Left returns) and the player bar (Down from the last
// row; Up returns). Returns the buttons left for the list itself, without
// Left and Right.
uint64_t appFocusFilter(App *app, uint64_t pressed, size_t cursor, size_t count, bool has_back, bool has_add);

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
// Collects the songs from source and opens the playlist picker.
void appAddToPlaylist(App *app, AddSource source);
// Asks to confirm removing the selected song from the open playlist.
void appConfirmRemoveSong(App *app);
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
// A on a row (or a tap): the sleep timer opens the number pad, others change.
void appActivateSetting(App *app, SettingRow row);
// Sends the equalizer and ReplayGain settings to the player.
void appApplyAudioSettings(App *app);
// The equalizer editor (app_eq.c).
void appHandleEqualizer(App *app, uint64_t pressed);
void appEqualizerSetGain(App *app, int band, int gain);
void appEqualizerNextPreset(App *app);
void appEqualizerToggle(App *app);
// Turns the screen off now (it comes back with any button or touch).
void appScreenOff(App *app);
// Pauses with a fade-out when the sleep timer runs out.
void appTickSleepTimer(App *app, uint64_t now_ns);

// Touch input (app_touch.c).
void appHandleTouch(App *app, uint64_t now_ns);
// True while a finger is on the screen.
bool appTouchDown(void);

// Handles buttons as if pressed this frame: overlays first, then the tab.
// Touch uses it to act like the matching button.
void appDispatchButtons(App *app, uint64_t pressed);

// Resume: restore the last queue at startup and save it while playing.
// play: keep playing; otherwise the restored queue waits paused.
void appRestoreSession(App *app, bool play);
void appSaveSession(App *app, uint64_t now_ns, bool force);
