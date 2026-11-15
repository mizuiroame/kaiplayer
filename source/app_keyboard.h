// Typing playlist names with the on-screen keyboard.
#pragma once

#include <stdint.h>

#include "app.h"

void appOpenKeyboard(App *app, NamePurpose purpose, const char *title, const char *initial);
void appHandleKeyboard(App *app, uint64_t pressed);
