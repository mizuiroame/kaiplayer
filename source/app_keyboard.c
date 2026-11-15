#include "app_keyboard.h"

#include <stdio.h>
#include <string.h>

#include "app_buttons.h"

void appOpenKeyboard(App *app, NamePurpose purpose, const char *title, const char *initial)
{
    keyboardInit(&app->keyboard, initial);
    app->keyboard_for = purpose;
    snprintf(app->keyboard_title, sizeof(app->keyboard_title), "%s", title);
    app->keyboard_open = true;
}

static void closeKeyboard(App *app)
{
    app->keyboard_open = false;
    trackListFree(&app->pending);
}

// Applies the typed name. Returns false to keep the keyboard open.
static bool applyName(App *app, const char *name)
{
    char message[160];
    PlaylistsTab *t = &app->playlists;
    if (!playlistNameValid(name)) {
        appToast(app, "Use letters, numbers, spaces and - _ . ( ) ' & ! + , (up to 48).");
        return false;
    }
    if (app->keyboard_for == NAME_FOR_RENAME) {
        const char *old = t->open ? t->name : t->index.names[t->cursor];
        if (strcmp(old, name) == 0)
            return true;  // Unchanged.
        if (!playlistRename(old, name)) {
            appToast(app, "A playlist with that name already exists.");
            return false;
        }
        if (t->open)
            snprintf(t->name, sizeof(t->name), "%s", name);
        snprintf(message, sizeof(message), "Renamed to \"%s\".", name);
    } else {
        if (!playlistCreate(name)) {
            appToast(app, "A playlist with that name already exists.");
            return false;
        }
        int added = app->keyboard_for == NAME_FOR_NEW_PLAYLIST_WITH_SONGS ? playlistAddTracks(name, &app->pending) : 0;
        if (added > 0)
            snprintf(message, sizeof(message), "Created \"%s\" with %d song%s.", name, added, added == 1 ? "" : "s");
        else
            snprintf(message, sizeof(message), "Created \"%s\".", name);
    }
    appToast(app, message);
    appPlaylistsRefresh(app);
    appPlaylistsSelect(app, name);
    return true;
}

static void finish(App *app)
{
    if (applyName(app, app->keyboard.text))
        closeKeyboard(app);
}

void appHandleKeyboard(App *app, uint64_t pressed)
{
    Keyboard *k = &app->keyboard;
    if (pressed & BUTTONS_UP) keyboardMove(k, 0, -1);
    if (pressed & BUTTONS_DOWN) keyboardMove(k, 0, 1);
    if (pressed & BUTTONS_LEFT) keyboardMove(k, -1, 0);
    if (pressed & BUTTONS_RIGHT) keyboardMove(k, 1, 0);
    if (pressed & HidNpadButton_X) keyboardToggleShift(k);
    if (pressed & HidNpadButton_Y) keyboardType(k, ' ');
    if (pressed & HidNpadButton_Minus) {
        closeKeyboard(app);
        return;
    }
    if (pressed & HidNpadButton_B) {
        if (k->text[0])
            keyboardBackspace(k);
        else
            closeKeyboard(app);  // B on an empty name cancels.
        return;
    }
    if ((pressed & HidNpadButton_Plus) || ((pressed & HidNpadButton_A) && keyboardPress(k) == KEYBOARD_DONE))
        finish(app);
}
