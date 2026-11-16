// The Playlists tab: the list of playlists, and the songs of an open one.
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "ui.h"

#define PAGE_ROWS UI_LIST_ROWS

static void moveCursor(size_t *cursor, long delta, size_t count)
{
    if (count == 0)
        return;
    long next = (long)*cursor + delta;
    if (next < 0)
        next = 0;
    if (next >= (long)count)
        next = (long)count - 1;
    *cursor = (size_t)next;
}

void appPlaylistsRefresh(App *app)
{
    PlaylistsTab *t = &app->playlists;
    playlistIndexFree(&t->index);
    if (!playlistIndexLoad(&t->index))
        appToast(app, "Could not read the playlists folder.");
    if (t->cursor >= t->index.count)
        t->cursor = t->index.count ? t->index.count - 1 : 0;
}

void appPlaylistsClose(App *app)
{
    PlaylistsTab *t = &app->playlists;
    trackListFree(&t->tracks);
    t->open = false;
    t->moving = false;
}

void appPlaylistsSelect(App *app, const char *name)
{
    PlaylistsTab *t = &app->playlists;
    for (size_t i = 0; i < t->index.count; i++) {
        if (strcmp(t->index.names[i], name) == 0) {
            t->cursor = i;
            return;
        }
    }
}

static bool loadPlaylist(App *app, const char *name, TrackList *out);

void appPlaylistsReloadIfOpen(App *app, const char *name)
{
    PlaylistsTab *t = &app->playlists;
    if (!t->open || strcmp(t->name, name) != 0)
        return;
    TrackList fresh;
    if (!loadPlaylist(app, name, &fresh))
        return;
    trackListFree(&t->tracks);
    t->tracks = fresh;
    if (t->track_cursor >= t->tracks.count)
        t->track_cursor = t->tracks.count ? t->tracks.count - 1 : 0;
}

static bool loadPlaylist(App *app, const char *name, TrackList *out)
{
    char path[UTIL_PATH_MAX];
    if (playlistPathFor(name, path, sizeof(path)) && playlistLoad(path, out))
        return true;
    appToast(app, "Could not read that playlist.");
    return false;
}

static void openSelected(App *app)
{
    PlaylistsTab *t = &app->playlists;
    if (t->cursor >= t->index.count)
        return;
    appPlaylistsClose(app);
    if (!loadPlaylist(app, t->index.names[t->cursor], &t->tracks))
        return;
    snprintf(t->name, sizeof(t->name), "%s", t->index.names[t->cursor]);
    t->open = true;
    t->track_cursor = 0;
}

// Plays a copy of the selected playlist, or of the open one.
static void playPlaylist(App *app, size_t start, bool shuffle)
{
    PlaylistsTab *t = &app->playlists;
    TrackList copy;
    bool ok = t->open ? trackListCopy(&copy, &t->tracks)
            : t->cursor < t->index.count && loadPlaylist(app, t->index.names[t->cursor], &copy);
    if (!ok)
        return;
    appPlayTracks(app, &copy, start, shuffle, t->open ? t->name : t->index.names[t->cursor]);
    trackListFree(&copy);
}

static void saveOpenPlaylist(App *app)
{
    PlaylistsTab *t = &app->playlists;
    char path[UTIL_PATH_MAX];
    if (!playlistPathFor(t->name, path, sizeof(path)) || !playlistSave(path, &t->tracks))
        appToast(app, "Could not save the playlist.");
}

static void handleMoving(App *app, uint64_t pressed)
{
    PlaylistsTab *t = &app->playlists;
    size_t from = t->track_cursor;
    if ((pressed & BUTTONS_UP) && from > 0)
        t->track_cursor = from - 1;
    if ((pressed & BUTTONS_DOWN) && from + 1 < t->tracks.count)
        t->track_cursor = from + 1;
    if (t->track_cursor != from)
        trackListMove(&t->tracks, from, t->track_cursor);
    if (pressed & (HidNpadButton_A | HidNpadButton_B)) {
        t->moving = false;
        saveOpenPlaylist(app);
    }
}

static void handleSongs(App *app, uint64_t pressed)
{
    PlaylistsTab *t = &app->playlists;
    if (t->moving) {
        handleMoving(app, pressed);
        return;
    }
    pressed = appFocusFilter(app, pressed, t->track_cursor, t->tracks.count, true, t->tracks.count > 0);
    if (pressed & BUTTONS_UP) moveCursor(&t->track_cursor, -1, t->tracks.count);
    if (pressed & BUTTONS_DOWN) moveCursor(&t->track_cursor, 1, t->tracks.count);
    if (pressed & BUTTONS_PAGE_UP) moveCursor(&t->track_cursor, -PAGE_ROWS, t->tracks.count);
    if (pressed & BUTTONS_PAGE_DOWN) moveCursor(&t->track_cursor, PAGE_ROWS, t->tracks.count);
    bool back = (pressed & HidNpadButton_B) || ((pressed & HidNpadButton_A) && app->focus == FOCUS_BACK);
    if (pressed & BUTTON_ROW_ACTION)
        appConfirmRemoveSong(app);  // Inside a playlist, the row button removes.
    else if ((pressed & HidNpadButton_A) && app->focus == FOCUS_LIST)
        playPlaylist(app, t->track_cursor, false);
    if (pressed & HidNpadButton_X) playPlaylist(app, 0, false);
    if (pressed & HidNpadButton_Y) playPlaylist(app, 0, true);
    if (back) {
        app->focus = FOCUS_LIST;
        appPlaylistsClose(app);
        appPlaylistsRefresh(app);
    }
}

void appHandlePlaylists(App *app, uint64_t pressed)
{
    PlaylistsTab *t = &app->playlists;
    if (t->open) {
        handleSongs(app, pressed);
        return;
    }
    pressed = appFocusFilter(app, pressed, t->cursor, t->index.count, false, false);
    if (pressed & BUTTONS_UP) moveCursor(&t->cursor, -1, t->index.count);
    if (pressed & BUTTONS_DOWN) moveCursor(&t->cursor, 1, t->index.count);
    if (pressed & BUTTONS_PAGE_UP) moveCursor(&t->cursor, -PAGE_ROWS, t->index.count);
    if (pressed & BUTTONS_PAGE_DOWN) moveCursor(&t->cursor, PAGE_ROWS, t->index.count);
    if (pressed & HidNpadButton_A) openSelected(app);
    if (pressed & HidNpadButton_X) playPlaylist(app, 0, false);
    if (pressed & HidNpadButton_Y) playPlaylist(app, 0, true);
}
