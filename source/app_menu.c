// The + menu: actions for the current tab, the playlist picker and the
// delete confirmation. Names are typed on the keyboard (app_keyboard.c).
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "app_keyboard.h"
#include "library.h"
#include "player.h"

#define MENU_PAGE 8

typedef enum {
    ACT_EXIT,           // Asks to confirm.
    ACT_EXIT_CONFIRMED,
    ACT_ADD,            // arg: AddSource. Opens the picker.
    ACT_PICK,           // arg: playlist index. Adds the pending songs.
    ACT_PICK_NEW,       // Names a new playlist for the pending songs.
    ACT_SAVE_QUEUE,
    ACT_NEW,
    ACT_RENAME,
    ACT_DELETE,
    ACT_DELETE_CONFIRMED,
    ACT_CANCEL,
    ACT_MOVE_SONG,
    ACT_REMOVE_SONG,
} MenuAction;

typedef enum {
    SRC_BROWSE_ENTRY,
    SRC_QUEUE_ENTRY,
    SRC_NOW_PLAYING,
} AddSource;

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
            menuAdd(&app->menu, "Move song", ACT_MOVE_SONG, 0);
            menuAdd(&app->menu, "Remove song from playlist", ACT_REMOVE_SONG, 0);
        }
    } else {
        menuAdd(&app->menu, "New playlist", ACT_NEW, 0);
    }
    const char *name = currentPlaylist(app);
    if (name) {
        snprintf(label, sizeof(label), "Rename \"%s\"", name);
        menuAdd(&app->menu, label, ACT_RENAME, 0);
        snprintf(label, sizeof(label), "Delete \"%s\"", name);
        menuAdd(&app->menu, label, ACT_DELETE, 0);
    }
}

void appOpenMenu(App *app)
{
    PlayerStatus status;
    playerGetStatus(&status);
    menuOpen(&app->menu, "Menu");
    app->menu.kind = MENU_KIND_MAIN;
    const BrowserEntry *entry = browserSelected(&app->browser);
    switch (app->view) {
    case VIEW_BROWSER:
        if (entry)
            menuAdd(&app->menu, entry->is_dir ? "Add folder to playlist" : "Add song to playlist", ACT_ADD,
                    SRC_BROWSE_ENTRY);
        break;
    case VIEW_PLAYLISTS:
        addPlaylistItems(app);
        break;
    case VIEW_QUEUE:
        if (status.track_count > 0) {
            menuAdd(&app->menu, "Add song to playlist", ACT_ADD, SRC_QUEUE_ENTRY);
            menuAdd(&app->menu, "Save queue as playlist", ACT_SAVE_QUEUE, 0);
        }
        break;
    default:
        if (status.track_count > 0)
            menuAdd(&app->menu, "Add this song to playlist", ACT_ADD, SRC_NOW_PLAYING);
        break;
    }
    menuAdd(&app->menu, "Exit KaiPlayer", ACT_EXIT, 0);
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
    }
    return false;
}

static void openPicker(App *app)
{
    char title[MENU_LABEL_MAX];
    if (app->pending.count == 1)
        snprintf(title, sizeof(title), "Add \"%s\" to", utilFileName(app->pending.paths[0]));
    else
        snprintf(title, sizeof(title), "Add %zu songs to", app->pending.count);
    appPlaylistsRefresh(app);
    menuOpen(&app->menu, title);
    menuAdd(&app->menu, "New playlist...", ACT_PICK_NEW, 0);
    for (size_t i = 0; i < app->playlists.index.count; i++) {
        if (!menuAdd(&app->menu, app->playlists.index.names[i], ACT_PICK, (int)i))
            break;
    }
}

static void addPendingTo(App *app, const char *name)
{
    int added = playlistAddTracks(name, &app->pending);
    char message[160];
    if (added < 0)
        snprintf(message, sizeof(message), "Could not add to \"%s\".", name);
    else if (added == 0)
        snprintf(message, sizeof(message), "Already in \"%s\".", name);
    else
        snprintf(message, sizeof(message), "Added %d song%s to \"%s\".", added, added == 1 ? "" : "s", name);
    appToast(app, message);
    if (added > 0)
        appPlaylistsReloadIfOpen(app, name);
    trackListFree(&app->pending);
}

static void startAdd(App *app, AddSource source)
{
    if (!collectSongs(app, source) || app->pending.count == 0) {
        appToast(app, "There are no songs to add.");
        trackListFree(&app->pending);
        return;
    }
    openPicker(app);
}

static void confirmDelete(App *app)
{
    char title[MENU_LABEL_MAX];
    snprintf(title, sizeof(title), "Delete \"%s\"? The songs stay on the card.", currentPlaylist(app));
    menuOpen(&app->menu, title);
    menuAdd(&app->menu, "Delete", ACT_DELETE_CONFIRMED, 0);
    menuAdd(&app->menu, "Cancel", ACT_CANCEL, 0);
}

static void confirmExit(App *app)
{
    menuOpen(&app->menu, "Exit KaiPlayer?");
    app->menu.kind = MENU_KIND_EXIT;
    menuAdd(&app->menu, "Exit", ACT_EXIT_CONFIRMED, 0);
    menuAdd(&app->menu, "Cancel", ACT_CANCEL, 0);
}

static void deletePlaylist(App *app)
{
    const char *name = currentPlaylist(app);
    char message[160];
    snprintf(message, sizeof(message), playlistDelete(name) ? "Deleted \"%s\"." : "Could not delete \"%s\".", name);
    appToast(app, message);
    appPlaylistsClose(app);
    appPlaylistsRefresh(app);
}

static void removeSong(App *app)
{
    PlaylistsTab *t = &app->playlists;
    char path[UTIL_PATH_MAX];
    trackListRemove(&t->tracks, t->track_cursor);
    if (t->track_cursor >= t->tracks.count && t->track_cursor > 0)
        t->track_cursor--;
    if (playlistPathFor(t->name, path, sizeof(path)) && playlistSave(path, &t->tracks))
        appToast(app, "Removed from the playlist.");
    else
        appToast(app, "Could not save the playlist.");
}

static void saveQueue(App *app)
{
    TrackList queue;
    size_t track;
    double position;
    trackListFree(&app->pending);
    if (!playerSnapshot(&queue, &track, &position)) {
        appToast(app, "The queue is empty.");
        return;
    }
    app->pending = queue;
    appOpenKeyboard(app, NAME_FOR_NEW_PLAYLIST_WITH_SONGS, "Name for the new playlist", "");
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
    case ACT_ADD: startAdd(app, (AddSource)item->arg); break;
    case ACT_PICK:
        if ((size_t)item->arg < app->playlists.index.count)
            addPendingTo(app, app->playlists.index.names[item->arg]);
        break;
    case ACT_PICK_NEW:
        appOpenKeyboard(app, NAME_FOR_NEW_PLAYLIST_WITH_SONGS, "Name for the new playlist", "");
        break;
    case ACT_SAVE_QUEUE: saveQueue(app); break;
    case ACT_NEW: appOpenKeyboard(app, NAME_FOR_NEW_PLAYLIST, "Name for the new playlist", ""); break;
    case ACT_RENAME: appOpenKeyboard(app, NAME_FOR_RENAME, "New name", currentPlaylist(app)); break;
    case ACT_DELETE: confirmDelete(app); break;
    case ACT_DELETE_CONFIRMED: deletePlaylist(app); break;
    case ACT_CANCEL: break;
    case ACT_MOVE_SONG:
        app->playlists.moving = true;
        appToast(app, "Move the song with Up/Down, then press A.");
        break;
    case ACT_REMOVE_SONG: removeSong(app); break;
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
    return false;
}
