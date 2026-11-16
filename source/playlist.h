// Playlists as M3U/M3U8 files in the "Playlists" folder of the music folder
// (sdmc:/playlists before one is chosen). Paths are written relative to the
// playlist's folder, so other players can read the files.
//
// A playlist's name can have any characters ("Español", "松原みき"), but its
// file gets a plain one the Switch 2 can list ("Espanol.m3u8"), with the
// name in a "#PLAYLIST:" line. The rest of the app uses the names.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "tracklist.h"

#define PLAYLIST_ROOT "sdmc:/playlists"
#define PLAYLIST_FOLDER_NAME "Playlists"
#define PLAYLIST_EXT ".m3u8"
#define PLAYLIST_NAME_CHARS 48
#define PLAYLIST_NAME_MAX (PLAYLIST_NAME_CHARS * 4)  // Bytes of UTF-8.
#define PLAYLIST_FILE_NAME_MAX 48
#define PLAYLIST_FILE_MAX (4u << 20)
#define PLAYLIST_MAX_COUNT 512

// Parses playlist text: one path per line; "#" lines, blank lines and a
// UTF-8 BOM are skipped. Relative paths resolve against base_dir.
bool playlistParse(const char *text, size_t length, const char *base_dir, TrackList *out);

// Formats a list as M3U8 text with paths relative to base_dir, and a
// "#PLAYLIST:" line when title isn't NULL. Caller frees.
char *playlistFormat(const TrackList *list, const char *base_dir, const char *title);

// Reads and writes playlist files. Saving replaces the file atomically and
// keeps its "#PLAYLIST:" name.
bool playlistLoad(const char *path, TrackList *out);
bool playlistSave(const char *path, const TrackList *list);

// The folder playlists live in. Tests point it elsewhere.
void playlistSetRoot(const char *root);
const char *playlistRoot(void);

// True if name can name a playlist: 1-48 characters of UTF-8, no control
// characters, / or \, not starting with a dot or a space, not ending in one.
bool playlistNameValid(const char *name);

// Names of the playlists in the root folder, sorted.
typedef struct {
    char **names;
    size_t count;
} PlaylistIndex;

bool playlistIndexLoad(PlaylistIndex *index);
void playlistIndexFree(PlaylistIndex *index);

// The file of the playlist called name, or the one a new playlist would get.
bool playlistPathFor(const char *name, char *out, size_t size);
bool playlistExists(const char *name);
// Creates an empty playlist. False if the name is invalid or taken (names
// that differ only in case count as the same).
bool playlistCreate(const char *name);
bool playlistRename(const char *name, const char *new_name);
bool playlistDelete(const char *name);
// Appends tracks to a playlist. Returns how many were added; tracks already
// in the playlist are skipped. -1 on error.
int playlistAddTracks(const char *name, const TrackList *tracks);
