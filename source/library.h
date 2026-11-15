// Collects the audio files under a folder into a sorted track list.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define LIBRARY_MAX_TRACKS 8192
#define LIBRARY_MAX_DEPTH 8
// Directory entries a scan may read in total, so huge trees stay bounded.
#define LIBRARY_MAX_ENTRIES 200000

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
} TrackList;

// True for a system or homebrew folder directly in a device root, such as
// "switch" in "sdmc:/". A recursive scan skips these; scanning one directly
// still works.
bool libraryIsSystemFolder(const char *parent, const char *name);

// Scans folder (and its subfolders when recursive). The list is empty, not
// an error, when the folder has no audio. Returns false if memory runs out.
bool libraryScan(TrackList *out, const char *folder, bool recursive);
void trackListFree(TrackList *list);

// Index of path in the list, or list->count if absent.
size_t trackListFind(const TrackList *list, const char *path);
