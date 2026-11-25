// State of the application shared by input handling and drawing, and the
// handlers that act on it (app_*.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "browser.h"
#include "catalog.h"
#include "config.h"
#include "keyboard.h"
#include "menu.h"
#include "notice.h"
#include "history.h"
#include "home.h"
#include "kinetic.h"
#include "player.h"
#include "playlist.h"
#include "settings.h"
#include "sysinfo.h"
#include "touch.h"
#include "tracklist.h"

typedef enum {
    VIEW_HOME,
    VIEW_BROWSER,  // The Library tab (its Folders page is the old Browse).
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

// The Library tab's pages. Root lists Artists, Albums, Songs and Folders.
typedef enum {
    LIB_ROOT,
    LIB_ARTISTS,
    LIB_ARTIST,   // arg: artist index; lists the artist's albums.
    LIB_ALBUMS,
    LIB_ALBUM,    // arg: album index; lists its songs.
    LIB_SONGS,
    LIB_FOLDERS,  // The folder browser.
} LibraryLevel;

#define LIB_ROOT_ROWS 4
#define LIBRARY_DEPTH_MAX 6

typedef struct {
    LibraryLevel level;
    size_t arg;
    size_t cursor;
    char name[TAGS_TEXT_MAX];    // The artist's name, or the album's title,
    char artist[TAGS_TEXT_MAX];  // and its artist: arg found again after a rescan.
} LibraryPage;

typedef struct {
    LibraryPage stack[LIBRARY_DEPTH_MAX];  // stack[depth - 1] is shown.
    int depth;
    unsigned generation;  // The catalog the pages' indexes belong to.
} LibraryTab;

// Which song is playing, to count it as played once.
typedef struct {
    unsigned generation;
    size_t track;
    bool armed;  // Not counted yet.
} HomeTracker;

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
    SRC_LIBRARY_ENTRY,  // The selected artist, album or song in Library.
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
    LibraryTab library;
    History history;            // Plays, for Home.
    HomeTracker home_tracker;
    Home home;                  // Home's rows, from the catalog and history.
    unsigned home_generation;   // The catalog Home's rows were made from.
    size_t home_history;        // History length Home's rows were made from.
    bool home_ready;
    int home_row;               // Selected row (HomeRowKind) and card in each.
    size_t home_col[HOME_ROW_COUNT];
    Config config;
    size_t queue_cursor;
    Focus focus;
    uint64_t buttons_down;      // Pressed this frame, without auto-repeat.
    char playing_playlist[PLAYLIST_NAME_MAX + 1];  // Where the queue came from; empty for a folder.
    BarControl bar_control;     // Selected player bar control, with FOCUS_PLAYER.
    PlaylistsTab playlists;
    bool audio_ok;
    SysInfo sys;  // Clock and battery, refreshed by the main loop.
    Notices notices;            // Messages, shown one at a time in a bubble at the top.
    NoticeView notice_views[NOTICE_STACK + 1];  // The bubbles to draw this frame,
    int notice_count;           // and how many.
    unsigned update_seen;       // The update status's serial already said on screen.
    bool update_manual;         // Settings asked: say "up to date" and failures too.

    Menu menu;              // The + menu and the dialogs it opens.
    bool keyboard_open;
    Keyboard keyboard;
    NamePurpose keyboard_for;
    char keyboard_title[192];
    TrackList pending;      // Songs waiting for a playlist to be picked.
    bool settings_open;
    SettingRow settings_cursor;
    SettingsPage settings_page;
    bool settings_from_welcome; // Opened by the welcome on a page: B there goes back to it.
    bool eq_open;               // The equalizer editor (over Settings).
    bool about_open;            // About KaiPlayer (over Settings).
    bool picker_open;           // Choosing the music folder.
    bool welcome_open;          // The first run's "Where is your music?".
    int welcome_cursor;         // Its row (WelcomeRow).
    bool welcome_away;          // A row of it opened the picker or Settings: back to it if nothing was set.
    Browser picker;             // The folders shown in the picker.
    int eq_band;                // Selected band in the editor.
    bool screen_off;            // Screen off while music plays.
    bool touch_wakes;           // The touch that woke the screen: ignore until released.
    uint64_t last_input_ns;     // For turning the screen off when idle.
    int sleep_minutes;          // The sleep timer's length; 0 is off.
    uint64_t sleep_deadline_ns; // When the sleep timer fires; 0 if off.
    TouchTracker touch;
    Kinetic kinetic;            // A list following a finger, or gliding on after it.
    bool swallow_tap;           // A press stopped a glide: its tap doesn't open anything.
    bool scrubbing;             // Dragging on the progress bar.
    double scrub_fraction;      // Where the finger is on the bar, 0 to 1.
    bool quit;
} App;

// A short message in the notice bubble at the top: something to know, done, or failed.
void appNotify(App *app, NoticeKind kind, const char *message);
// The same, as something to know.
void appToast(App *app, const char *message);
// The newest notice's text ("" for none): the keyboard asks again with it.
const char *appLastNotice(const App *app);

// app_update.c: Settings > About's updates. Start: the daily look, if due (nro_path: argv[0]).
void appStartUpdates(App *app, const char *nro_path);
void appStopUpdates(void);
// Once per frame: what the look or the install found, said on screen.
void appUpdatesTick(App *app);
// A on "Look for an update": looks, or installs the version found.
void appUpdatesActivate(App *app);
// The daily look on or off.
void appUpdatesToggle(App *app);
// What the update rows show.
void appUpdatesValue(const App *app, int row, char *out, size_t size);

// Moves the focus between a list, its "< Back" (Up from the first row) and
// the player bar (Down from the last row; Up returns). Returns the buttons
// left for the list itself, without Left and Right; Right on a row becomes
// BUTTON_ROW_ACTION, its add (or remove) button.
uint64_t appFocusFilter(App *app, uint64_t pressed, size_t cursor, size_t count, bool has_back, bool has_add);

// The music folder (app_folder.c): Browse stays inside it ("sdmc:/" until
// one is chosen).
const char *appMusicRoot(const App *app);
void appOpenFolderPicker(App *app);

// app_welcome.c: the first run's welcome, which asks where the music is: a
// folder of the SD card, the network folder, the Navidrome server, or later.
typedef enum {
    WELCOME_CARD,
    WELCOME_NETWORK,
    WELCOME_SERVER,
    WELCOME_LATER,
    WELCOME_ROW_COUNT,
} WelcomeRow;
// True when nothing says where the music is yet and the welcome wasn't answered.
bool appWelcomeNeeded(const App *app);
void appOpenWelcome(App *app);
void appHandleWelcome(App *app, uint64_t pressed);
void appWelcomeChoose(App *app, WelcomeRow row);
// Once per frame: back to the welcome when what a row opened was left
// without a source set (a server that failed to sign in, a picker closed).
void appWelcomeTick(App *app);
// Where playlists go without a music folder: KaiPlayer's own folder.
void appAppPlaylistFolder(char *out, size_t size);
void appHandleFolderPicker(App *app, uint64_t pressed);
// Picker rows are folders only: how many of the listing's entries those are.
size_t appPickerFolderCount(const App *app);
void appPickerChoose(App *app);  // Use the folder the picker is in.
// Looks for music in the music folder again ("Update the library"), or says to choose one first.
void appUpdateLibrary(App *app);
// The library from the card's music folder, the network folder (its top is the music folder: "dav:/") or
// the Navidrome server ("sub:/"). One that isn't set up says so, and the library stays.
void appSetLibrarySource(App *app, LibrarySource source);
LibrarySource appLibrarySource(const App *app);
// Not the card's: the network folder's or the server's.
bool appLibraryIsRemote(const App *app);
// The card's always is; the others once set up (and the network is up).
bool appSourceReady(LibrarySource source);
// DAV_ROOT or SUB_ROOT; NULL for the card.
const char *appSourceRoot(LibrarySource source);
// Where X goes: the next source set up, card, network folder, Navidrome, card...
LibrarySource appNextLibrarySource(const App *app);
void appSwitchLibrarySource(App *app);
// There's another source to switch to (or this one isn't the card's): X switches, and the pill shows.
bool appCanSwitchSource(const App *app);
// A source on the network changed its account (moved: its address): its library is read again (in the
// background while another is shown), or, gone, what was found of it is forgotten and the card's shows.
void appLibraryRemoteChanged(App *app, LibrarySource source, bool moved);
// The header's pill of a library on the network being read (root: DAV_ROOT or SUB_ROOT): that library, shown.
void appRemoteScanTapped(App *app, const char *root);
// Lists the music folder's top for the Folders page, unless it's already in it.
void appOpenFolders(App *app);
// Points playlists at the music folder's Playlists folder, copying the ones
// in sdmc:/playlists there the first time.
void appUsePlaylistFolder(App *app);

// The Library tab (app_library.c, library_tab.c).
LibraryPage *appLibraryPage(App *app);
// Rows on the current page (c may be NULL while the catalog loads).
size_t appLibraryCount(const App *app, const Catalog *c);
void appLibraryBack(App *app);
// Keeps the pages pointing at the same artists and albums when the catalog
// is replaced (a rescan), and the cursors inside the lists. c may be NULL.
void appLibrarySync(App *app, const Catalog *c);
void appHandleLibrary(App *app, uint64_t pressed);
// The songs of the selected row, or of the whole page, in play order; start
// is the selected song's place in them.
bool appLibrarySongs(App *app, const Catalog *c, bool whole_page, TrackList *out, size_t *start);

// The Home tab and the play history (app_home.c; appHome in library_tab.c).
void appLoadHistory(App *app);
void appRecordPlay(App *app, HistoryKind kind, const char *key);
// Counts the playing song as played once it has played long enough.
void appTrackPlays(App *app, const PlayerStatus *status);
// Home's rows, worked out again when the catalog or history changed.
const Home *appHome(App *app, const Catalog *c);
void appHandleHome(App *app, uint64_t pressed);
// Plays the Library page that is open (an artist or an album).
void appLibraryPlayPage(App *app, bool shuffle);
// Opens or plays the playlist the Playlists tab has selected.
void appPlaylistsOpenSelected(App *app);
void appPlaylistsPlaySelected(App *app, bool shuffle);

// Tab handlers. pressed holds the buttons pressed this frame (with repeat).
void appHandleBrowser(App *app, uint64_t pressed);
// False at the music folder: Browse doesn't go above it.
bool appBrowseCanGoUp(const App *app);
void appHandlePlaylists(App *app, uint64_t pressed);

// Playlists tab helpers.
void appPlaylistsRefresh(App *app);
void appPlaylistsClose(App *app);
// Reloads the open playlist if it is `name`, after songs were added to it.
void appPlaylistsReloadIfOpen(App *app, const char *name);
// Puts the cursor on the playlist called `name`, if it exists.
void appPlaylistsSelect(App *app, const char *name);
// playlist: the playlist the songs come from, or NULL for a folder. Now
// Playing shows its name, and Auto ReplayGain treats it as mixed albums.
void appPlayTracks(App *app, TrackList *tracks, size_t start, bool shuffle, const char *playlist);

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
// Now Playing: the lyrics shown or not (kept in the settings).
void appToggleLyrics(App *app);
// Selects the item at index of the current tab's list and, unless
// select_only, opens it as A would.
void appActivateRow(App *app, size_t index, bool select_only);

// Settings page and sleep timer (app_settings.c).
void appOpenSettings(App *app);
// Settings, on one of its pages (B goes back to the main page from it).
void appOpenSettingsAt(App *app, SettingsPage page);
void appHandleSettings(App *app, uint64_t pressed);
// Finds the languages (English, those in the app, config/lang's files) and uses the one chosen.
void appStartLanguages(App *app);
// Uses app->config.language (English if it's gone), and names it for Settings.
void appUseLanguage(App *app);
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

// The network folder (app_network.c, dav.h): its Settings page, its test, and the console's network.
// Reads its account (network.txt) and starts the network if there is one. Before the library's scan.
void appNetworkStart(App *app);
// True when there is a network folder and the network is up.
bool appNetworkReady(void);
// A Settings row of its page: true if it was one.
bool appNetworkActivate(App *app, SettingRow row);
// Once a frame: a test's answer.
void appNetworkUpdate(App *app);
// Closes its connections and lets the console's network go, after everything that reads files has stopped.
void appNetworkExit(void);
// What its rows show: the address ("" for none) and the user.
void appNetworkShown(char *address, size_t address_size, char *user, size_t user_size);
// Its switches: the password kept on the card, signing not required; and whether it's an SMB folder.
void appNetworkChoices(bool *remember, bool *allow_unsigned, bool *smb, bool *check_certificate);
// Before using the network folder: its password, when it isn't remembered and hasn't been
// typed since KaiPlayer started, is asked for now. False if it wasn't given.
bool appNetworkEnsurePassword(App *app);
// A network folder's location as the person knows it: "192.168.0.10:8098/music/Album".
void appNetworkShownPath(const char *location, char *out, size_t size);

// The Navidrome server (app_server.c, subsonic.h): its Settings page, its test, the songs played told to it.
// Reads its account (navidrome.txt) and starts the network if there is one. Before the library's scan.
void appServerStart(App *app);
// True when there is a server signed in to, and the network is up.
bool appServerReady(void);
// A Settings row of its page (A, or a step): true if it was one.
bool appServerActivate(App *app, SettingRow row);
// Once a frame: a test's answer, and the playing song told to the server.
void appServerUpdate(App *app, const PlayerStatus *status);
// Stops its threads and closes its connections, after everything that reads files has stopped.
void appServerExit(void);
// What its rows show: the address ("" for none), the user ("" if not signed in), the certificate check.
void appServerShown(char *address, size_t address_size, char *user, size_t user_size, bool *check_certificate);

// Resume: restore the last queue at startup and save it while playing.
// play: keep playing; otherwise the restored queue waits paused.
void appRestoreSession(App *app, bool play);
void appSaveSession(App *app, uint64_t now_ns, bool force);
