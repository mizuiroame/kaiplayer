// What the console's file system says about a folder, beyond the listing.
#pragma once

// How many entries the file system counts in folder (files and folders),
// or -1 if it can't tell. On Switch 2 the listing leaves out names with
// accents or other non-ASCII characters; comparing the two finds them.
long fsinfoEntryCount(const char *folder);
