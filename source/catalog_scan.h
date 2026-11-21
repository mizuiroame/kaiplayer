// Keeps the catalog of the music folder up to date on a background thread.
// It shows the last saved catalog at once, then rescans the folder and only
// reads the tags of new or changed files. The folder may be the network
// folder's ("dav:/", kfile.h): its tags are read over the network once, and
// kept in a file of their own, by path, size and time.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "catalog.h"

bool catalogScanInit(void);
void catalogScanExit(void);

// Scans root (again). Cheap to call; a scan already running restarts.
void catalogScanStart(const char *root);
// The same, forgetting what was kept for root: the network folder is another one now.
void catalogScanStartFresh(const char *root);
// Reads the network folder's library in the background while the card's is
// shown (after the shown library's scan, which comes first), into its own
// file, so that it shows at once when it's chosen. fresh: forget what was kept
// (another address). Shown, it's that library's scan. A scan of the other kind
// stopped part way keeps what it read and is finished later.
void catalogScanRefreshNetwork(bool fresh);
// There is no network folder now: what was kept of it goes, and its scan stops.
void catalogScanForgetNetwork(void);
// True while the network folder's library is being read, shown or not, with
// its songs read so far and found (0 while its folders are still listed).
bool catalogScanNetworkProgress(size_t *done, size_t *total);

// Locks and returns the current catalog (NULL before the first one is
// ready); call catalogUnlock after using it.
const Catalog *catalogLock(void);
void catalogUnlock(void);

// True while scanning, with the files done and found so far.
bool catalogScanProgress(size_t *done, size_t *total);
// Why the last scan of the network folder failed ("" if it didn't), for the person.
void catalogScanProblem(char *out, size_t size);
