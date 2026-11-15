// Small path and string helpers shared by the browser, library and UI.
#pragma once

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>

#define UTIL_PATH_MAX 769

// True if path ends in "." + ext, ignoring case.
bool utilHasExtension(const char *path, const char *ext);

// True if the file name has an extension that a decoder handles.
bool utilIsAudioFile(const char *name);

// True if a directory entry inside folder is itself a folder.
bool utilEntryIsDir(const char *folder, const struct dirent *entry);

// The part of a path after its last '/'.
const char *utilFileName(const char *path);

// Writes dir + '/' + name into out. Returns false if it does not fit.
bool utilJoinPath(char *out, size_t size, const char *dir, const char *name);

// Removes the last component of a path ("sdmc:/a/b" -> "sdmc:/a").
// Returns false if the path is already a root ("sdmc:/").
bool utilParentPath(char *path);

// Collapses "." and ".." components and repeated slashes, in place.
// "sdmc:/a/./b/../c" -> "sdmc:/a/c". False if ".." goes above the root.
bool utilNormalizePath(char *path);

// Resolves a path read from a playlist against the playlist's folder:
// "sdmc:/x" stays absolute, "/x" is on base_dir's device, anything else is
// relative to base_dir. Backslashes count as slashes. The result is
// normalized. False if it doesn't fit or leaves the device root.
bool utilResolvePath(const char *base_dir, const char *ref, char *out, size_t size);

// Writes target relative to from_dir ("../Music/a.mp3"), or target itself
// when they are on different devices. Both must be normalized absolute paths.
bool utilRelativePath(const char *from_dir, const char *target, char *out, size_t size);

// Case-insensitive ASCII comparison, for sorting names.
int utilCompareNames(const char *a, const char *b);

// Reads one UTF-8 code point and advances *s. Bad bytes read as '?'.
// Never reads past a terminating NUL.
unsigned utilNextCodePoint(const char **s);

// Formats seconds as m:ss or h:mm:ss.
void utilFormatTime(char *out, size_t size, double seconds);
