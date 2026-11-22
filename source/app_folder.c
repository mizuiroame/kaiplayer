// The music folder: chosen once (or from Settings), and Browse stays in it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "catalog_scan.h"
#include "dav.h"
#include "lang.h"
#include "playlist.h"
#include "subsonic.h"

#define DEVICE_ROOT "sdmc:/"

const char *appMusicRoot(const App *app)
{
    if (app->config.source == LIBRARY_NETWORK)
        return DAV_ROOT;
    if (app->config.source == LIBRARY_SERVER)
        return SUB_ROOT;
    return app->config.library[0] ? app->config.library : DEVICE_ROOT;
}

LibrarySource appLibrarySource(const App *app)
{
    return app->config.source;
}

bool appLibraryIsRemote(const App *app)
{
    return app->config.source != LIBRARY_CARD;
}

bool appSourceReady(LibrarySource source)
{
    switch (source) {
    case LIBRARY_NETWORK: return appNetworkReady();
    case LIBRARY_SERVER: return appServerReady();
    default: return true;
    }
}

const char *appSourceRoot(LibrarySource source)
{
    return source == LIBRARY_NETWORK ? DAV_ROOT : source == LIBRARY_SERVER ? SUB_ROOT : NULL;
}

LibrarySource appNextLibrarySource(const App *app)
{
    for (int step = 1; step < LIBRARY_SOURCE_COUNT; step++) {
        LibrarySource next = (LibrarySource)((app->config.source + step) % LIBRARY_SOURCE_COUNT);
        if (appSourceReady(next))
            return next;
    }
    return LIBRARY_CARD;
}

bool appCanSwitchSource(const App *app)
{
    return appNetworkReady() || appServerReady() || appLibraryIsRemote(app);
}

void appSwitchLibrarySource(App *app)
{
    appSetLibrarySource(app, appNextLibrarySource(app));
}

// The Folders page shows the music folder's top again, listed when it's opened
// (the network folder's waits for its server).
static void resetFolders(App *app)
{
    browserFree(&app->browser);
    app->browser.path[0] = '\0';
    app->browser.problem[0] = '\0';
}

void appOpenFolders(App *app)
{
    Browser *b = &app->browser;
    const char *root = appMusicRoot(app);
    bool listed = b->path[0] && utilPathWithin(b->path, root) && !(b->count == 0 && b->problem[0]);
    if (listed || browserOpen(b, root))
        return;
    char problem[BROWSER_PROBLEM_MAX];
    snprintf(problem, sizeof(problem), "%s", b->problem);
    browserFree(b);  // Empty at the top, saying why.
    snprintf(b->path, sizeof(b->path), "%s", root);
    snprintf(b->problem, sizeof(b->problem), "%s", problem[0] ? problem : T("Could not open the folder."));
}

void appSetLibrarySource(App *app, LibrarySource source)
{
    if (!appSourceReady(source)) {
        appToast(app, source == LIBRARY_SERVER ? T("Sign in to the Navidrome server in Settings first.")
                                               : T("Set up the network folder in Settings first."));
        return;
    }
    if (app->config.source == source)
        return;
    app->config.source = source;
    configSave(&app->config);
    app->library.depth = 0;  // Back to the Library's first page.
    app->focus = FOCUS_LIST;
    resetFolders(app);
    if (source != LIBRARY_CARD || app->config.library[0])
        catalogScanStart(appMusicRoot(app));
    appToast(app, source == LIBRARY_NETWORK  ? T("Library: the network folder")
                  : source == LIBRARY_SERVER ? T("Library: Navidrome")
                                             : T("Library: the SD card"));
}

void appLibraryRemoteChanged(App *app, LibrarySource source, bool moved)
{
    const char *root = appSourceRoot(source);
    if (!root)
        return;
    if (!appSourceReady(source)) {  // Forgotten: what was found of it goes, and the card's library shows.
        catalogScanForgetRemote(root);
        if (app->config.source == source)
            appSetLibrarySource(app, LIBRARY_CARD);
        return;
    }
    if (app->config.source != source) {
        // Just set up (a server signed in, a folder set): its library is the one shown, as KaiVideo does
        // after a sign-in, and it's read now with the pill. Not doing so left the old one on screen.
        app->config.source = source;
        configSave(&app->config);
        app->focus = FOCUS_LIST;
        appToast(app, source == LIBRARY_NETWORK ? T("Library: the network folder") : T("Library: Navidrome"));
    }
    app->library.depth = 0;
    resetFolders(app);
    if (moved)
        catalogScanStartFresh(root);
    else
        catalogScanStart(root);  // Another user: look again.
}

void appRemoteScanTapped(App *app, const char *root)
{
    LibrarySource source = subIsLocation(root) ? LIBRARY_SERVER : LIBRARY_NETWORK;
    appSetLibrarySource(app, source);
    if (app->config.source == source)
        appSetView(app, VIEW_BROWSER);
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
    if (!app->config.library[0] && !appLibraryIsRemote(app)) {
        appToast(app, T("Choose your music folder first."));
        return;
    }
    catalogScanStart(appMusicRoot(app));
    appToast(app, T("Updating the library..."));  // Its progress shows on the Library tab.
}

void appPickerChoose(App *app)
{
    snprintf(app->config.library, sizeof(app->config.library), "%s", app->picker.path);
    snprintf(app->config.folder, sizeof(app->config.folder), "%s", app->picker.path);
    app->config.source = LIBRARY_CARD;  // A card folder chosen: the card's library.
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
