// Button groups shared by the input handlers.
#pragma once

#include <switch.h>

#define BUTTONS_UP (HidNpadButton_Up | HidNpadButton_StickLUp)
#define BUTTONS_DOWN (HidNpadButton_Down | HidNpadButton_StickLDown)
#define BUTTONS_LEFT (HidNpadButton_Left | HidNpadButton_StickLLeft)
#define BUTTONS_RIGHT (HidNpadButton_Right | HidNpadButton_StickLRight)
// The right stick: up/down move a list by a page, left/right seek.
#define BUTTONS_PAGE_UP HidNpadButton_StickRUp
#define BUTTONS_PAGE_DOWN HidNpadButton_StickRDown
#define BUTTONS_REPEATING (BUTTONS_UP | BUTTONS_DOWN | BUTTONS_LEFT | BUTTONS_RIGHT | \
                           BUTTONS_PAGE_UP | BUTTONS_PAGE_DOWN | \
                           HidNpadButton_StickRLeft | HidNpadButton_StickRRight)
