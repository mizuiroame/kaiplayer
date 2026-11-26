// Helpers shared by stream.c and streamparse.c (not for the rest of the app).
#pragma once

#include <stdbool.h>
#include <stddef.h>

// text into out, cut at a whole UTF-8 character if it doesn't fit, control characters as spaces.
void streamCopyText(char *out, size_t size, const char *text);
// 1 to max ASCII digits.
bool streamAllDigits(const char *s, size_t max);
// An Archive identifier: letters, digits, '.', '-', '_' (at most 100); not "." or "..".
bool streamIdentifierValid(const char *s);
// The extension of a file KaiPlayer plays from these services, in lowercase ("mp3"), or NULL.
const char *streamPlayableExtension(const char *file);
