#include "app_keyboard.h"

#include <stdio.h>
#include <string.h>

#include "app_buttons.h"
#include "keyboard_system.h"
#include "lang.h"

static void closeKeyboard(App *app)
{
    app->keyboard_open = false;
    trackListFree(&app->pending);
}

static bool applyName(App *app, const char *name);

// Removes leading and trailing spaces in place.
static void trimSpaces(char *text)
{
    size_t start = strspn(text, " ");
    size_t len = strlen(text + start);
    memmove(text, text + start, len + 1);
    while (len > 0 && text[len - 1] == ' ')
        text[--len] = '\0';
}

void appOpenKeyboard(App *app, NamePurpose purpose, const char *title, const char *initial)
{
    app->keyboard_for = purpose;
    char text[KEYBOARD_MAX * 4 + 1], typed[KEYBOARD_MAX * 4 + 1], prompt[512];
    snprintf(text, sizeof(text), "%s", initial ? initial : "");
    snprintf(prompt, sizeof(prompt), "%s", title);
    // The console's keyboard first. If the name is refused, it opens again
    // with what was typed and the reason as its title.
    for (;;) {
        SystemKeyboardResult result = systemKeyboardInput(prompt, text, typed, sizeof(typed), KEYBOARD_MAX);
        if (result == SYSTEM_KEYBOARD_UNAVAILABLE)
            break;
        if (result == SYSTEM_KEYBOARD_CANCELLED) {
            closeKeyboard(app);
            return;
        }
        trimSpaces(typed);
        if (applyName(app, typed)) {
            closeKeyboard(app);
            return;
        }
        snprintf(text, sizeof(text), "%s", typed);
        snprintf(prompt, sizeof(prompt), "%s", appLastNotice(app));
    }
    // Fallback: KaiPlayer's own keyboard.
    keyboardInit(&app->keyboard, text);
    snprintf(app->keyboard_title, sizeof(app->keyboard_title), "%s", title);
    app->keyboard_open = true;
}

// Applies the typed name. Returns false to keep the keyboard open.
static bool applyName(App *app, const char *name)
{
    char message[512];
    PlaylistsTab *t = &app->playlists;
    if (!playlistNameValid(name)) {
        appToast(app, T("Up to 48 characters, without / or \\, not starting with a dot."));
        return false;
    }
    if (app->keyboard_for == NAME_FOR_RENAME) {
        const char *old = t->open ? t->name : t->index.names[t->cursor];
        if (strcmp(old, name) == 0)
            return true;  // Unchanged.
        if (!playlistRename(old, name)) {
            appNotify(app, NOTICE_ERROR, T("A playlist with that name already exists."));
            return false;
        }
        if (t->open)
            snprintf(t->name, sizeof(t->name), "%s", name);
        snprintf(message, sizeof(message), T("Renamed to \"%s\"."), name);
    } else {
        if (!playlistCreate(name)) {
            appNotify(app, NOTICE_ERROR, T("A playlist with that name already exists."));
            return false;
        }
        int added = app->keyboard_for == NAME_FOR_NEW_PLAYLIST_WITH_SONGS ? playlistAddTracks(name, &app->pending) : 0;
        if (added > 0)
            snprintf(message, sizeof(message),
                     added == 1 ? T("Created \"%s\" with %d song.") : T("Created \"%s\" with %d songs."), name, added);
        else
            snprintf(message, sizeof(message), T("Created \"%s\"."), name);
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
