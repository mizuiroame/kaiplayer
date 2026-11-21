// The + menu: actions for the current tab, the playlist picker and the
// delete confirmation. Names are typed on the keyboard (app_keyboard.c).
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "app_keyboard.h"
#include "catalog_scan.h"
#include "lang.h"
#include "library.h"
#include "meta.h"
#include "player.h"

#define MENU_PAGE 8

typedef enum {
    ACT_EXIT,           // Asks to confirm.
    ACT_EXIT_CONFIRMED,
    ACT_SETTINGS,
    ACT_SCREEN_OFF,
    ACT_RESCAN,
    ACT_PICK,           // arg: playlist index. Adds the pending songs.
    ACT_PICK_NEW,       // Names a new playlist for the pending songs.
    ACT_SAVE_QUEUE,
    ACT_NEW,
    ACT_RENAME,
    ACT_DELETE,
    ACT_DELETE_CONFIRMED,
    ACT_CANCEL,
    ACT_MOVE_SONG,
    ACT_REMOVE_SONG,    // Asks to confirm.
    ACT_REMOVE_CONFIRMED,
} MenuAction;

// The playlist the menu acts on: the open one, or the selected one.
static const char *currentPlaylist(const App *app)
{
    const PlaylistsTab *t = &app->playlists;
    if (t->open)
        return t->name;
    return t->cursor < t->index.count ? t->index.names[t->cursor] : NULL;
}

static void addPlaylistItems(App *app)
{
    PlaylistsTab *t = &app->playlists;
    char label[MENU_LABEL_MAX];
    if (t->open) {
        if (t->tracks.count > 0) {
            menuAdd(&app->menu, T("Move song"), ACT_MOVE_SONG, 0);
            menuAdd(&app->menu, T("Remove song from playlist"), ACT_REMOVE_SONG, 0);
        }
    } else {
        menuAdd(&app->menu, T("New playlist"), ACT_NEW, 0);
    }
    const char *name = currentPlaylist(app);
    if (name) {
        snprintf(label, sizeof(label), T("Rename \"%s\""), name);
        menuAdd(&app->menu, label, ACT_RENAME, 0);
        snprintf(label, sizeof(label), T("Delete \"%s\""), name);
        menuAdd(&app->menu, label, ACT_DELETE, 0);
    }
}

// First this screen's actions, then a line and the app's, the same on every
// screen (as KaiVideo's and KaiRadio's): Settings, then Exit last.
void appOpenMenu(App *app)
{
    PlayerStatus status;
    playerGetStatus(&status);
    menuOpen(&app->menu, "KaiPlayer");  // With its version beside it (ui_overlay.c).
    app->menu.kind = MENU_KIND_MAIN;
    // Adding songs to a playlist is on the + buttons of each row and of Now
    // Playing, so the menu keeps the actions that have no button.
    if (app->view == VIEW_PLAYLISTS)
        addPlaylistItems(app);
    else if (app->view == VIEW_QUEUE && status.track_count > 0)
        menuAdd(&app->menu, T("Save queue as playlist"), ACT_SAVE_QUEUE, 0);
    menuAddDivider(&app->menu);
    if (app->config.library[0])
        menuAdd(&app->menu, T("Update the library"), ACT_RESCAN, 0);
    if (status.state == PLAYER_PLAYING)
        menuAdd(&app->menu, T("Turn screen off (music keeps playing)"), ACT_SCREEN_OFF, 0);
    menuAdd(&app->menu, T("Settings"), ACT_SETTINGS, 0);
    menuAdd(&app->menu, T("Exit KaiPlayer"), ACT_EXIT, 0);
}

// Fills app->pending with the songs an "Add" item refers to.
static bool collectSongs(App *app, AddSource source)
{
    trackListFree(&app->pending);
    char path[UTIL_PATH_MAX];
    size_t track;
    PlayerStatus status;
    playerGetStatus(&status);
    const BrowserEntry *entry = browserSelected(&app->browser);
    switch (source) {
    case SRC_BROWSE_ENTRY:
        if (!entry || !utilJoinPath(path, sizeof(path), app->browser.path, entry->name))
            return false;
        if (entry->is_dir)
            return libraryScan(&app->pending, path, true);
        return trackListAdd(&app->pending, path);
    case SRC_QUEUE_ENTRY:
        return playerQueueTrack(app->queue_cursor, &track) && playerTrackPath(track, path, sizeof(path)) &&
               trackListAdd(&app->pending, path);
    case SRC_NOW_PLAYING:
        return playerTrackPath(status.track, path, sizeof(path)) && trackListAdd(&app->pending, path);
    case SRC_LIBRARY_ENTRY: {
        // A song row adds that song; an artist or album row adds all of it.
        const Catalog *c = catalogLock();
        TrackList all;
        size_t start = 0;
        LibraryLevel level = appLibraryPage(app)->level;
        bool ok = c && appLibrarySongs(app, c, false, &all, &start);
        catalogUnlock();
        if (ok && (level == LIB_ALBUM || level == LIB_SONGS))
            ok = start < all.count && trackListAdd(&app->pending, all.paths[start]);
        else if (ok)
            ok = trackListAddAll(&app->pending, &all);
        trackListFree(&all);
        return ok;
    }
    case SRC_PLAYLIST_SONG: {
        const PlaylistsTab *t = &app->playlists;
        return t->open && t->track_cursor < t->tracks.count &&
               trackListAdd(&app->pending, t->tracks.paths[t->track_cursor]);
    }
    }
    return false;
}

#define SONG_TITLE_MAX 44  // Room left for a title in a menu heading.

// Drops a UTF-8 character cut off at the end of s.
static void trimPartialUtf8(char *s)
{
    size_t len = strlen(s), start = len;
    while (start > 0 && ((unsigned char)s[start - 1] & 0xC0) == 0x80)
        start--;  // Back over continuation bytes to the character's lead byte.
    if (start == 0 || ((unsigned char)s[start - 1] & 0x80) == 0)
        return;   // Ends in ASCII.
    unsigned char lead = (unsigned char)s[start - 1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    if (len - (start - 1) < need)
        s[start - 1] = '\0';
}

// A song's title from its tags, or its file name without the extension,
// shortened to fit a menu heading.
static void songTitle(const char *path, char out[SONG_TITLE_MAX])
{
    const size_t size = SONG_TITLE_MAX;
    static SongMeta meta;  // Big; menus run on the UI thread.
    if (metaLookup(path, &meta) && meta.title[0]) {
        snprintf(out, size, "%.43s", meta.title);
    } else {
        snprintf(out, size, "%.43s", utilFileName(path));
        char *dot = strrchr(out, '.');
        if (dot && dot != out)
            *dot = '\0';
    }
    trimPartialUtf8(out);
}

static void openPicker(App *app)
{
    char title[MENU_LABEL_MAX], song[SONG_TITLE_MAX];
    if (app->pending.count == 1) {
        songTitle(app->pending.paths[0], song);
        snprintf(title, sizeof(title), T("Add \"%s\" to"), song);
    }
    else
        snprintf(title, sizeof(title), T("Add %zu songs to"), app->pending.count);
    appPlaylistsRefresh(app);
    menuOpen(&app->menu, title);
    menuAdd(&app->menu, T("New playlist..."), ACT_PICK_NEW, 0);
    for (size_t i = 0; i < app->playlists.index.count; i++) {
        if (!menuAdd(&app->menu, app->playlists.index.names[i], ACT_PICK, (int)i))
            break;
    }
}

static void addPendingTo(App *app, const char *name)
{
    int added = playlistAddTracks(name, &app->pending);
    char message[512];
    char song[SONG_TITLE_MAX];
    if (added < 0) {
        snprintf(message, sizeof(message), T("Could not add to \"%s\"."), name);
    } else if (added == 0) {
        snprintf(message, sizeof(message), T("Already in \"%s\"."), name);
    } else if (app->pending.count == 1) {
        songTitle(app->pending.paths[0], song);
        snprintf(message, sizeof(message), T("Added \"%s\" to \"%s\"."), song, name);
    } else {
        snprintf(message, sizeof(message), added == 1 ? T("Added %d song to \"%s\".") : T("Added %d songs to \"%s\"."),
                 added, name);
    }
    appToast(app, message);
    if (added > 0)
        appPlaylistsReloadIfOpen(app, name);
    trackListFree(&app->pending);
}

void appAddToPlaylist(App *app, AddSource source)
{
    if (!collectSongs(app, source) || app->pending.count == 0) {
        appToast(app, T("There are no songs to add."));
        trackListFree(&app->pending);
        return;
    }
    openPicker(app);
}

static void confirmDelete(App *app)
{
    char title[MENU_LABEL_MAX];
    snprintf(title, sizeof(title), T("Delete \"%s\"? The songs stay on the card."), currentPlaylist(app));
    menuOpen(&app->menu, title);
    menuAdd(&app->menu, T("Delete"), ACT_DELETE_CONFIRMED, 0);
    menuAdd(&app->menu, T("Cancel"), ACT_CANCEL, 0);
}

static void confirmExit(App *app)
{
    menuOpen(&app->menu, T("Exit KaiPlayer?"));
    app->menu.kind = MENU_KIND_EXIT;
    menuAdd(&app->menu, T("Exit"), ACT_EXIT_CONFIRMED, 0);
    menuAdd(&app->menu, T("Cancel"), ACT_CANCEL, 0);
}

static void deletePlaylist(App *app)
{
    const char *name = currentPlaylist(app);
    char message[512];
    snprintf(message, sizeof(message), playlistDelete(name) ? T("Deleted \"%s\".") : T("Could not delete \"%s\"."),
             name);
    appToast(app, message);
    appPlaylistsClose(app);
    appPlaylistsRefresh(app);
}

void appConfirmRemoveSong(App *app)
{
    const PlaylistsTab *t = &app->playlists;
    if (!t->open || t->track_cursor >= t->tracks.count)
        return;
    char song[SONG_TITLE_MAX], title[MENU_LABEL_MAX];
    songTitle(t->tracks.paths[t->track_cursor], song);
    snprintf(title, sizeof(title), T("Remove \"%s\" from this playlist?"), song);
    menuOpen(&app->menu, title);
    menuAdd(&app->menu, T("Remove"), ACT_REMOVE_CONFIRMED, 0);
    menuAdd(&app->menu, T("Cancel"), ACT_CANCEL, 0);
}

static void removeSong(App *app)
{
    PlaylistsTab *t = &app->playlists;
    if (t->track_cursor >= t->tracks.count)
        return;
    char path[UTIL_PATH_MAX], song[SONG_TITLE_MAX], message[320];
    songTitle(t->tracks.paths[t->track_cursor], song);
    trackListRemove(&t->tracks, t->track_cursor);
    if (t->track_cursor >= t->tracks.count && t->track_cursor > 0)
        t->track_cursor--;
    if (playlistPathFor(t->name, path, sizeof(path)) && playlistSave(path, &t->tracks))
        snprintf(message, sizeof(message), T("Removed \"%s\" from \"%s\"."), song, t->name);
    else
        snprintf(message, sizeof(message), "%s", T("Could not save the playlist."));
    appToast(app, message);
}

static void saveQueue(App *app)
{
    TrackList queue;
    size_t track;
    double position;
    trackListFree(&app->pending);
    if (!playerSnapshot(&queue, &track, &position)) {
        appToast(app, T("The queue is empty."));
        return;
    }
    app->pending = queue;
    appOpenKeyboard(app, NAME_FOR_NEW_PLAYLIST_WITH_SONGS, T("Name for the new playlist"), "");
}

// Runs the chosen item. Items that open another menu or the keyboard keep
// the overlay open; the rest close it.
static void runItem(App *app, const MenuItem *item)
{
    MenuAction action = (MenuAction)item->action;
    menuClose(&app->menu);
    switch (action) {
    case ACT_EXIT: confirmExit(app); break;
    case ACT_EXIT_CONFIRMED: app->quit = true; break;
    case ACT_SETTINGS: appOpenSettings(app); break;
    case ACT_SCREEN_OFF: appScreenOff(app); break;
    case ACT_RESCAN: appUpdateLibrary(app); break;
    case ACT_PICK:
        if ((size_t)item->arg < app->playlists.index.count)
            addPendingTo(app, app->playlists.index.names[item->arg]);
        break;
    case ACT_PICK_NEW:
        appOpenKeyboard(app, NAME_FOR_NEW_PLAYLIST_WITH_SONGS, T("Name for the new playlist"), "");
        break;
    case ACT_SAVE_QUEUE: saveQueue(app); break;
    case ACT_NEW: appOpenKeyboard(app, NAME_FOR_NEW_PLAYLIST, T("Name for the new playlist"), ""); break;
    case ACT_RENAME: appOpenKeyboard(app, NAME_FOR_RENAME, T("New name"), currentPlaylist(app)); break;
    case ACT_DELETE: confirmDelete(app); break;
    case ACT_DELETE_CONFIRMED: deletePlaylist(app); break;
    case ACT_CANCEL: break;
    case ACT_MOVE_SONG:
        app->playlists.moving = true;
        appToast(app, T("Move the song with Up/Down, then press A."));
        break;
    case ACT_REMOVE_SONG: appConfirmRemoveSong(app); break;
    case ACT_REMOVE_CONFIRMED: removeSong(app); break;
    }
}

static void handleMenu(App *app, uint64_t pressed)
{
    Menu *m = &app->menu;
    // + in the + menu asks to exit; + once more in that dialog exits.
    if ((pressed & HidNpadButton_Plus) && m->kind == MENU_KIND_MAIN) {
        confirmExit(app);
        return;
    }
    if ((pressed & HidNpadButton_Plus) && m->kind == MENU_KIND_EXIT) {
        app->quit = true;
        return;
    }
    if (pressed & BUTTONS_UP) menuMove(m, -1);
    if (pressed & BUTTONS_DOWN) menuMove(m, 1);
    if (pressed & BUTTONS_LEFT) menuMove(m, -MENU_PAGE);
    if (pressed & BUTTONS_RIGHT) menuMove(m, MENU_PAGE);
    if (pressed & HidNpadButton_B) {
        menuClose(m);
        trackListFree(&app->pending);
    } else if (pressed & HidNpadButton_A) {
        const MenuItem *item = menuSelected(m);
        if (item) {
            MenuItem chosen = *item;  // runItem may reopen the menu.
            runItem(app, &chosen);
        }
    }
}

bool appHandleOverlay(App *app, uint64_t pressed)
{
    if (app->keyboard_open) {
        appHandleKeyboard(app, pressed);
        return true;
    }
    if (app->menu.open) {
        handleMenu(app, pressed);
        return true;
    }
    if (app->picker_open) {
        appHandleFolderPicker(app, pressed);
        return true;
    }
    if (app->eq_open) {
        appHandleEqualizer(app, pressed);
        return true;
    }
    if (app->about_open) {
        if (pressed & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
            app->about_open = false;  // Back to Settings.
        return true;
    }
    if (app->settings_open) {
        appHandleSettings(app, pressed);
        return true;
    }
    return false;
}
