// The first run's welcome: where is the music? A folder of the SD card, the
// network folder, the Navidrome server, or later (from Settings). Nothing has
// to be chosen: without a music folder, playlists stay in KaiPlayer's folder.
#include <stdio.h>

#include "app.h"
#include "app_buttons.h"
#include "lang.h"

// Somewhere to read music from: a music folder, or the network folder or
// server chosen as the library.
static bool sourceSet(const App *app)
{
    return app->config.library[0] || appLibraryIsRemote(app);
}

bool appWelcomeNeeded(const App *app)
{
    return !app->config.welcomed && !sourceSet(app);
}

void appOpenWelcome(App *app)
{
    app->welcome_open = true;
    app->welcome_away = false;
    if (app->welcome_cursor < 0 || app->welcome_cursor >= WELCOME_ROW_COUNT)
        app->welcome_cursor = WELCOME_CARD;
}

static void answered(App *app)
{
    app->welcome_open = false;
    app->welcome_away = false;
    app->config.welcomed = true;
    configSave(&app->config);
}

// Said once the welcome is answered without a music folder: where playlists go.
static void tellPlaylistFolder(App *app)
{
    char folder[UTIL_PATH_MAX], message[UTIL_PATH_MAX + 96];
    appAppPlaylistFolder(folder, sizeof(folder));
    snprintf(message, sizeof(message), T("Playlists are kept in %s until you choose a music folder."), folder);
    appToast(app, message);
}

void appWelcomeChoose(App *app, WelcomeRow row)
{
    app->welcome_cursor = row;
    switch (row) {
    case WELCOME_CARD:
        app->welcome_open = false;
        app->welcome_away = true;
        appOpenFolderPicker(app);
        if (!app->picker_open)
            app->welcome_open = true;  // The card couldn't be read: stay here.
        break;
    case WELCOME_NETWORK:
    case WELCOME_SERVER:
        app->welcome_open = false;
        app->welcome_away = true;
        appOpenSettingsAt(app, row == WELCOME_NETWORK ? SETTINGS_PAGE_NETWORK : SETTINGS_PAGE_SERVER);
        break;
    case WELCOME_LATER:
        answered(app);
        tellPlaylistFolder(app);
        break;
    default: break;
    }
}

void appHandleWelcome(App *app, uint64_t pressed)
{
    if ((pressed & BUTTONS_UP) && app->welcome_cursor > 0)
        app->welcome_cursor--;
    if ((pressed & BUTTONS_DOWN) && app->welcome_cursor + 1 < WELCOME_ROW_COUNT)
        app->welcome_cursor++;
    if (pressed & HidNpadButton_A)
        appWelcomeChoose(app, (WelcomeRow)app->welcome_cursor);
    else if (pressed & HidNpadButton_B)
        appWelcomeChoose(app, WELCOME_LATER);  // B answers "later", as the row does.
}

void appWelcomeTick(App *app)
{
    if (app->welcome_open) {
        if (sourceSet(app))
            answered(app);  // A sign-in that finished after Settings was closed.
        return;
    }
    if (!app->welcome_away || app->picker_open || app->settings_open || app->keyboard_open || app->menu.open)
        return;
    if (sourceSet(app)) {  // A folder chosen, a server signed in: nothing more to ask.
        answered(app);
        return;
    }
    appOpenWelcome(app);  // Left without one (a sign-in failed, the picker closed): choose again.
}
