// The music folder: chosen once (or from Settings), and Browse stays in it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "catalog_scan.h"
#include "config.h"
#include "dav.h"
#include "lang.h"
#include "playlist.h"
#include "streamfile.h"
#include "subsonic.h"

#define DEVICE_ROOT "sdmc:/"

// The Internet Archive has no catalog: under it, the card's music folder stays the one the catalog knows.
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
    return app->config.source == LIBRARY_NETWORK || app->config.source == LIBRARY_SERVER;
}

bool appLibraryIsArchive(const App *app)
{
    return app->config.source == LIBRARY_ARCHIVE;
}

bool appSourceReady(LibrarySource source)
{
    switch (source) {
    case LIBRARY_NETWORK: return appNetworkReady();
    case LIBRARY_SERVER: return appServerReady();
    case LIBRARY_ARCHIVE: return streamFileEnabled();  // Nothing to set up: on, or off in Settings.
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
    return appNetworkReady() || appServerReady() || streamFileEnabled() || app->config.source != LIBRARY_CARD;
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
        appToast(app, source == LIBRARY_SERVER    ? T("Sign in to the Navidrome server in Settings first.")
                      : source == LIBRARY_ARCHIVE ? T("The Internet Archive is turned off in Settings.")
                                                  : T("Set up the network folder in Settings first."));
        return;
    }
    if (app->config.source == source)
        return;
    if (source == LIBRARY_NETWORK)
        appNetworkEnsurePassword(app);  // Without it, what was read before shows.
    bool from_archive = app->config.source == LIBRARY_ARCHIVE;
    app->config.source = source;
    configSave(&app->config);
    app->library.depth = 0;  // Back to the Library's first page.
    app->focus = FOCUS_LIST;
    resetFolders(app);
    if (source == LIBRARY_ARCHIVE)
        appStreamEnsureNetwork(app);  // Browsed: no catalog to read; the card's stays loaded underneath.
    else if (source != LIBRARY_CARD || app->config.library[0] || from_archive)
        catalogScanStart(appMusicRoot(app));
    appToast(app, source == LIBRARY_NETWORK   ? T("Library: the network folder")
                  : source == LIBRARY_SERVER  ? T("Library: Navidrome")
                  : source == LIBRARY_ARCHIVE ? T("Library: the Internet Archive")
                  : app->config.library[0]    ? T("Library: the SD card")
                                              : T("Library: the whole SD card (no music folder chosen)"));
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

void appOpenFolderPicker(App *app)
{
    const char *start = app->config.library[0] ? app->config.library : DEVICE_ROOT;
    if (!browserOpen(&app->picker, start) && !browserOpen(&app->picker, DEVICE_ROOT)) {
        appNotify(app, NOTICE_ERROR, T("Could not read the SD card."));
        return;
    }
    app->picker_open = true;
}

void appAppPlaylistFolder(char *out, size_t size)
{
    if (!utilJoinPath(out, size, configFolder(), PLAYLIST_FOLDER_NAME))
        snprintf(out, size, "%s", PLAYLIST_ROOT);
}

void appUsePlaylistFolder(App *app)
{
    backgroundLoad(app->config.library, app->config.background, app->config.background_blur);  // Its folder is in the music folder.
    char own[UTIL_PATH_MAX];
    appAppPlaylistFolder(own, sizeof(own));
    char folder[UTIL_PATH_MAX];
    if (!app->config.library[0])
        snprintf(folder, sizeof(folder), "%s", own);  // No music folder: KaiPlayer's own, until one is chosen.
    else if (!utilJoinPath(folder, sizeof(folder), app->config.library, PLAYLIST_FOLDER_NAME))
        return;
    playlistSetRoot(folder);
    if (!app->config.playlists_moved) {  // Once, wherever they go now: the oldest place, sdmc:/playlists.
        playlistCopyAll(PLAYLIST_ROOT, false);
        app->config.playlists_moved = true;
        configSave(&app->config);
    }
    if (app->config.library[0])
        playlistCopyAll(own, true);  // Those made before a music folder was chosen move into it.
    appPlaylistsRefresh(app);
}

void appUpdateLibrary(App *app)
{
    if (appLibraryIsArchive(app)) {  // Asked each time a page opens: nothing to read again.
        appToast(app, T("The Internet Archive is asked each time you open a page: nothing to update."));
        return;
    }
    if (!app->config.library[0] && !appLibraryIsRemote(app)) {
        appToast(app, T("Choose your music folder first."));
        return;
    }
    if (app->config.source == LIBRARY_NETWORK && !appNetworkEnsurePassword(app))
        return;
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
            appNotify(app, NOTICE_ERROR, T("Could not open the folder."));
    }
    if (pressed & HidNpadButton_B) {
        if (!browserGoUp(b))
            app->picker_open = false;  // At the top: close (from the welcome, back to it).
    }
    if (pressed & HidNpadButton_X)
        appPickerChoose(app);
}
