// Keeps the catalog of the music folder up to date on a background thread.
// It shows the last saved catalog at once, then rescans the folder and only
// reads the tags of new or changed files.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "catalog.h"

bool catalogScanInit(void);
void catalogScanExit(void);

// Scans root (again). Cheap to call; a scan already running restarts.
void catalogScanStart(const char *root);

// Locks and returns the current catalog (NULL before the first one is
// ready); call catalogUnlock after using it.
const Catalog *catalogLock(void);
void catalogUnlock(void);

// True while scanning, with the files done and found so far.
bool catalogScanProgress(size_t *done, size_t *total);
