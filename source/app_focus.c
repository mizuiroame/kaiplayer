// Button focus on a list's extra controls: "< Back" above the first row and
// the "+" at the end of each row.
#include "app.h"
#include "app_buttons.h"

uint64_t appFocusFilter(App *app, uint64_t pressed, size_t cursor, bool has_back, bool has_add)
{
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
    if ((pressed & BUTTONS_RIGHT) && has_add && app->focus == FOCUS_LIST)
        app->focus = FOCUS_ADD;
    if ((pressed & BUTTONS_LEFT) && app->focus == FOCUS_ADD)
        app->focus = FOCUS_LIST;
    return pressed & ~(BUTTONS_LEFT | BUTTONS_RIGHT);
}
