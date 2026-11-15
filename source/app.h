// State of the application shared by input handling and drawing.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "browser.h"
#include "config.h"

typedef enum {
    VIEW_BROWSER,
    VIEW_QUEUE,
} View;

typedef struct {
    View view;
    Browser browser;
    Config config;
    size_t queue_cursor;
    bool audio_ok;
    char toast[160];
    uint64_t toast_until_ns;
} App;

// Shows a short message above the player bar.
void appToast(App *app, const char *message);
