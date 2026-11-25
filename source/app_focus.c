// Button focus on a list's extra controls: "< Back" above the first row,
// the "+" at the end of each row, and the player bar below the list.
#include "app.h"
#include "app_buttons.h"
#include "player.h"

// With a book playing, the bar's shuffle and repeat are its speed and chapters.
static void pressBarControl(App *app, BarControl control)
{
    PlayerStatus status;
    playerGetStatus(&status);
    bool book = appBookPlaying(app, &status);
    switch (control) {
    case BAR_PREV: appTransportPrev(app); break;
    case BAR_PLAY: playerTogglePause(); break;
    case BAR_NEXT: appTransportNext(app); break;
    case BAR_SHUFFLE:
        if (book)
            appBooksOpenSpeed(app);
        else
            playerToggleShuffle();
        break;
    default:
        if (book)
            appBooksOpenChapters(app, -1);
        else
            playerCycleRepeat();
        break;
    }
}

// In the player bar, Left/Right choose a control, A presses it, and Up or B
// go back to the list. The list gets none of these.
static uint64_t filterPlayerBar(App *app, uint64_t pressed)
{
    if ((pressed & BUTTONS_LEFT) && app->bar_control > 0)
        app->bar_control--;
    if ((pressed & BUTTONS_RIGHT) && app->bar_control + 1 < BAR_COUNT)
        app->bar_control++;
    if (pressed & HidNpadButton_A)
        pressBarControl(app, app->bar_control);
    if (pressed & (BUTTONS_UP | HidNpadButton_B))
        app->focus = FOCUS_LIST;
    return pressed & ~(BUTTONS_UP | BUTTONS_DOWN | BUTTONS_LEFT | BUTTONS_RIGHT | HidNpadButton_A | HidNpadButton_B);
}

uint64_t appFocusFilter(App *app, uint64_t pressed, size_t cursor, size_t count, bool has_back, bool has_add)
{
    if (app->focus == FOCUS_PLAYER)
        return filterPlayerBar(app, pressed);
    if ((pressed & BUTTONS_DOWN) && app->focus != FOCUS_BACK && cursor + 1 >= count) {
        app->focus = FOCUS_PLAYER;  // Down from the last row (or an empty list).
        app->bar_control = BAR_PLAY;
        return pressed & ~(BUTTONS_DOWN | BUTTONS_LEFT | BUTTONS_RIGHT);
    }
    if (app->focus == FOCUS_BACK && !has_back)
        app->focus = FOCUS_LIST;
    if (app->focus == FOCUS_ADD && !has_add)
        app->focus = FOCUS_LIST;

    if (app->focus == FOCUS_BACK) {
        // On Back, Down returns to the list; other directions stay.
        if (pressed & BUTTONS_DOWN) {
            app->focus = FOCUS_LIST;
            pressed &= ~BUTTONS_DOWN;
        }
        pressed &= ~(BUTTONS_UP | BUTTONS_LEFT | BUTTONS_RIGHT);
        return pressed;
    }
    if ((pressed & BUTTONS_UP) && cursor == 0 && has_back) {
        app->focus = FOCUS_BACK;  // Keeps any "+" focus out of the way.
        pressed &= ~BUTTONS_UP;
    }
    if (app->focus == FOCUS_ADD)
        app->focus = FOCUS_LIST;  // → now acts at once, so the "+" is never left selected.
    // → presses the row's button straight away, once per press (holding it
    // doesn't page through the picker or dialog it opens).
    // Only the d-pad: the left stick just moves, so a nudge doesn't add or remove.
    uint64_t row_action = (pressed & app->buttons_down & HidNpadButton_Right) && has_add ? BUTTON_ROW_ACTION : 0;
    return (pressed & ~(BUTTONS_LEFT | BUTTONS_RIGHT)) | row_action;
}
