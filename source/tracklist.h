// A list of track paths: a scanned folder, a playlist or the play queue.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define TRACKLIST_MAX 8192

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
} TrackList;

void trackListFree(TrackList *list);

// Appends a copy of path. A full list (TRACKLIST_MAX) ignores it and still
// returns true; false means out of memory.
bool trackListAdd(TrackList *list, const char *path);

// Appends copies of every path in src.
bool trackListAddAll(TrackList *list, const TrackList *src);

// Copies src into a new list. out is empty on failure.
bool trackListCopy(TrackList *out, const TrackList *src);

// Removes the entry at index. Out of range does nothing.
void trackListRemove(TrackList *list, size_t index);

// Moves the entry at from to position to, shifting the ones between.
void trackListMove(TrackList *list, size_t from, size_t to);

// Index of path in the list, or list->count if absent.
size_t trackListFind(const TrackList *list, const char *path);
