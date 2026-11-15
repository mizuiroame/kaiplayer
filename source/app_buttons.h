// Button groups shared by the input handlers.
#pragma once

#include <switch.h>

#define BUTTONS_UP (HidNpadButton_Up | HidNpadButton_StickLUp)
#define BUTTONS_DOWN (HidNpadButton_Down | HidNpadButton_StickLDown)
#define BUTTONS_LEFT (HidNpadButton_Left | HidNpadButton_StickLLeft)
#define BUTTONS_RIGHT (HidNpadButton_Right | HidNpadButton_StickLRight)
#define BUTTONS_REPEATING (BUTTONS_UP | BUTTONS_DOWN | BUTTONS_LEFT | BUTTONS_RIGHT | \
                           HidNpadButton_StickRLeft | HidNpadButton_StickRRight)
