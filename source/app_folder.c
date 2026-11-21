// The music folder: chosen once (or from Settings), and Browse stays in it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "catalog_scan.h"
#include "lang.h"
#include "playlist.h"

#define DEVICE_ROOT "sdmc:/"

const char *appMusicRoot(const App *app)
{
    return app->config.library[0] ? app->config.library : DEVICE_ROOT;
}

size_t appPickerFolderCount(const App *app)
{
    size_t n = 0;
    while (n < app->picker.count && app->picker.entries[n].is_dir)
        n++;  // Folders sort first.
    return n;
}

void appOpenFolderPicker(App *app, bool first_run)
{
    const char *start = app->config.library[0] ? app->config.library : DEVICE_ROOT;
    if (!browserOpen(&app->picker, start) && !browserOpen(&app->picker, DEVICE_ROOT)) {
        appToast(app, T("Could not read the SD card."));
        return;
    }
    app->picker_open = true;
    app->picker_first_run = first_run;
}

// Copies the playlists of sdmc:/playlists into the current folder,
// rewriting their relative paths for the new place. The old files stay.
static void copyOldPlaylists(void)
{
    char target[UTIL_PATH_MAX];
    snprintf(target, sizeof(target), "%s", playlistRoot());
    playlistSetRoot(PLAYLIST_ROOT);
    PlaylistIndex old;
    bool have = playlistIndexLoad(&old);
    TrackList *lists = have && old.count ? calloc(old.count, sizeof(TrackList)) : NULL;
    for (size_t i = 0; lists && i < old.count; i++) {
        char path[UTIL_PATH_MAX];
        if (playlistPathFor(old.names[i], path, sizeof(path)))
            playlistLoad(path, &lists[i]);
    }
    playlistSetRoot(target);
    for (size_t i = 0; lists && i < old.count; i++) {
        char path[UTIL_PATH_MAX];
        if (!playlistExists(old.names[i]) && playlistCreate(old.names[i]) &&
            playlistPathFor(old.names[i], path, sizeof(path)))
            playlistSave(path, &lists[i]);
        trackListFree(&lists[i]);
    }
    free(lists);
    if (have)
        playlistIndexFree(&old);
}

void appUsePlaylistFolder(App *app)
{
    backgroundLoad(app->config.library, app->config.background, app->config.background_blur);  // Its folder is in the music folder.
    if (!app->config.library[0]) {
        playlistSetRoot(PLAYLIST_ROOT);
        return;
    }
    char folder[UTIL_PATH_MAX];
    if (!utilJoinPath(folder, sizeof(folder), app->config.library, PLAYLIST_FOLDER_NAME))
        return;
    playlistSetRoot(folder);
    if (!app->config.playlists_moved) {
        copyOldPlaylists();
        app->config.playlists_moved = true;
        configSave(&app->config);
    }
    appPlaylistsRefresh(app);
}

void appUpdateLibrary(App *app)
{
    if (!app->config.library[0]) {
        appToast(app, T("Choose your music folder first."));
        return;
    }
    catalogScanStart(app->config.library);
    appToast(app, T("Updating the library..."));  // Its progress shows on the Library tab.
}

void appPickerChoose(App *app)
{
    snprintf(app->config.library, sizeof(app->config.library), "%s", app->picker.path);
    snprintf(app->config.folder, sizeof(app->config.folder), "%s", app->picker.path);
    configSave(&app->config);
    browserOpen(&app->browser, app->config.library);
    app->picker_open = false;
    app->library.depth = 0;  // Back to the Library's first page.
    appUsePlaylistFolder(app);
    catalogScanStart(app->config.library);
    char message[192];
    snprintf(message, sizeof(message), T("Music folder: %.60s"),
             utilFileName(app->picker.path)[0] ? utilFileName(app->picker.path) : app->picker.path);
    appToast(app, message);
}

// A opens a folder, B goes up (or closes at the top), X uses this folder.
void appHandleFolderPicker(App *app, uint64_t pressed)
{
    Browser *b = &app->picker;
    size_t folders = appPickerFolderCount(app);
    if ((pressed & BUTTONS_UP) && b->cursor > 0)
        b->cursor--;
    if ((pressed & BUTTONS_DOWN) && b->cursor + 1 < folders)
        b->cursor++;
    if ((pressed & HidNpadButton_A) && b->cursor < folders) {
        char path[UTIL_PATH_MAX];
        if (!utilJoinPath(path, sizeof(path), b->path, b->entries[b->cursor].name) || !browserOpen(b, path))
            appToast(app, T("Could not open the folder."));
    }
    if (pressed & HidNpadButton_B) {
        if (!browserGoUp(b) && !app->picker_first_run)
            app->picker_open = false;  // At the top: close (the first time, a folder is needed).
    }
    if (pressed & HidNpadButton_X)
        appPickerChoose(app);
}
