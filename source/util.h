// Small path and string helpers shared by the browser, library and UI.
#pragma once

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>

#define UTIL_PATH_MAX 769

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

// Case-insensitive ASCII comparison, for sorting names.
int utilCompareNames(const char *a, const char *b);

// Formats seconds as m:ss or h:mm:ss.
void utilFormatTime(char *out, size_t size, double seconds);
