// The Browse tab: navigate the card and play folders.
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "kfile.h"
#include "lang.h"
#include "library.h"
#include "ui.h"

#define PAGE_ROWS UI_LIST_ROWS

// Scans folder and plays it, from start_name if given.
static void playFolder(App *app, const char *folder, bool recursive, const char *start_name, bool shuffle)
{
    TrackList list;
    if (!libraryScan(&list, folder, recursive)) {
        appNotify(app, NOTICE_ERROR, kfileIsRemote(folder) ? T("The network folder couldn't be read.")
                                            : T("Out of memory for the track list."));
        return;
    }
    libraryDropBooks(&list);  // They play from Audiobooks, not with the music.
    if (list.count == 0) {
        trackListFree(&list);
        appToast(app, T("No music in this folder."));
        return;
    }
    size_t start = 0;
    char path[UTIL_PATH_MAX];
    if (start_name && utilJoinPath(path, sizeof(path), folder, start_name)) {
        size_t found = trackListFind(&list, path);
        start = found < list.count ? found : 0;
    }
    appPlayTracks(app, &list, start, shuffle, NULL);
    snprintf(app->config.folder, sizeof(app->config.folder), "%s", folder);
    configSave(&app->config);
}

static void openSelected(App *app)
{
    const BrowserEntry *e = browserSelected(&app->browser);
    if (!e)
        return;
    if (!e->is_dir) {
        playFolder(app, app->browser.path, false, e->name, false);
        return;
    }
    char path[UTIL_PATH_MAX];
    if (!utilJoinPath(path, sizeof(path), app->browser.path, e->name) ||
        !browserOpen(&app->browser, path))  // A network folder's says why.
        appNotify(app, NOTICE_ERROR, app->browser.problem[0] ? app->browser.problem : T("Could not open the folder."));
}

bool appBrowseCanGoUp(const App *app)
{
    char parent[UTIL_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", app->browser.path);
    return utilParentPath(parent) && utilPathWithin(parent, appMusicRoot(app));
}

void appHandleBrowser(App *app, uint64_t pressed)
{
    Browser *b = &app->browser;
    bool can_go_up = appBrowseCanGoUp(app);
    bool has_back = true;  // At the music folder, Back returns to Library.
    pressed = appFocusFilter(app, pressed, b->cursor, b->count, has_back, b->count > 0);
    if (pressed & BUTTONS_UP) browserMove(b, -1);
    if (pressed & BUTTONS_DOWN) browserMove(b, 1);
    if (pressed & BUTTONS_PAGE_UP) browserMove(b, -PAGE_ROWS);
    if (pressed & BUTTONS_PAGE_DOWN) browserMove(b, PAGE_ROWS);
    if (pressed & BUTTON_ROW_ACTION)
        appAddToPlaylist(app, SRC_BROWSE_ENTRY);
    if (pressed & HidNpadButton_A) {
        if (app->focus == FOCUS_BACK) {
            if (can_go_up)
                browserGoUp(b);
            else
                appLibraryBack(app);
            app->focus = FOCUS_LIST;
        } else {
            openSelected(app);
        }
    }
    if ((pressed & HidNpadButton_B) && !can_go_up) {
        appLibraryBack(app);
    } else if (pressed & HidNpadButton_B) {
        browserGoUp(b);
        app->focus = FOCUS_LIST;
    }
    if (pressed & HidNpadButton_X) playFolder(app, b->path, true, NULL, false);
    if (pressed & HidNpadButton_Y) playFolder(app, b->path, true, NULL, true);
}
