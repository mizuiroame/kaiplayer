// The folder listing that the user navigates: folders first, then audio files.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "util.h"

// Entries listed per folder; the rest are left out.
#define BROWSER_MAX_ENTRIES 16384
#define BROWSER_PROBLEM_MAX 192

typedef struct {
    char *name;
    bool is_dir;
} BrowserEntry;

typedef struct {
    char path[UTIL_PATH_MAX];
    BrowserEntry *entries;
    size_t count;
    size_t cursor;
    bool incomplete;  // Some entries could not be read, or the list was capped.
    char problem[BROWSER_PROBLEM_MAX];  // Why the last network folder couldn't be listed; "" if it could.
} Browser;

// Lists path: a folder on the card, or in the network folder ("dav:/...",
// through kfile.h, which waits for the server). On failure the browser keeps
// its previous listing (problem says why, for a network folder).
bool browserOpen(Browser *b, const char *path);
void browserFree(Browser *b);

// Opens the parent folder and puts the cursor on the folder that was left.
bool browserGoUp(Browser *b);

// Moves the cursor by delta, clamped to the list.
void browserMove(Browser *b, int delta);

const BrowserEntry *browserSelected(const Browser *b);
