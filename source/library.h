// Collects the audio files under a folder into a sorted track list.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tracklist.h"

#define LIBRARY_MAX_TRACKS TRACKLIST_MAX
#define LIBRARY_MAX_DEPTH 8
// Directory entries a scan may read in total, so huge trees stay bounded.
#define LIBRARY_MAX_ENTRIES 200000
// A network folder's scan asks for at most this many folders.
#define LIBRARY_NETWORK_FOLDERS 4000

// True for a system or homebrew folder directly in a device root, such as
// "switch" in "sdmc:/". A recursive scan skips these; scanning one directly
// still works.
bool libraryIsSystemFolder(const char *parent, const char *name);

// Scans folder (and its subfolders when recursive). The list is empty, not
// an error, when the folder has no audio. Returns false if memory runs out,
// or if a network folder (kfile.h) can't be listed.
bool libraryScan(TrackList *out, const char *folder, bool recursive);

// A network folder's song, as its listing says.
typedef struct {
    char *path;        // "dav:/Artist/Album/01 Song.flac"
    int64_t size;      // Bytes; -1 if unknown.
    int64_t modified;  // Seconds since 1970; 0 if unknown.
} LibraryFile;

typedef struct {
    LibraryFile *files;  // By path, as libraryScan sorts.
    size_t count, capacity;
} LibraryFiles;

// The songs under a network folder ("dav:/" or deeper), one listing per
// folder. stop (may be NULL) is asked between folders: true gives up. False
// if the folder itself can't be listed (why, if not NULL, says why), it was
// stopped, or memory ran out.
bool libraryScanNetwork(LibraryFiles *out, const char *folder, bool (*stop)(void), char *why, size_t why_size);
void libraryFilesFree(LibraryFiles *files);
