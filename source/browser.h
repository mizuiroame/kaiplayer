// The folder listing that the user navigates: folders first, then audio files.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "util.h"

// Entries listed per folder; the rest are left out.
#define BROWSER_MAX_ENTRIES 16384

typedef struct {
    char *name;
    bool is_dir;
} BrowserEntry;

typedef struct {
    char path[UTIL_PATH_MAX];
    BrowserEntry *entries;
    size_t count;
    size_t cursor;
} Browser;

// Lists path. On failure the browser keeps its previous listing.
bool browserOpen(Browser *b, const char *path);
void browserFree(Browser *b);

// Opens the parent folder and puts the cursor on the folder that was left.
bool browserGoUp(Browser *b);

// Moves the cursor by delta, clamped to the list.
void browserMove(Browser *b, int delta);

const BrowserEntry *browserSelected(const Browser *b);
