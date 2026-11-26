// Helpers shared by stream.c and streamparse.c (not for the rest of the app).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define STREAM_NO_LICENSE "-"
#define STREAM_LMA_LICENSE "lma"      // The Live Music Archive's bands: non-commercial.
#define STREAM_78_LICENSE "78rpm"     // The 78 rpm collection's records, with no license said.

// text into out, cut at a whole UTF-8 character if it doesn't fit, control characters as spaces.
void streamCopyText(char *out, size_t size, const char *text);
// An Archive identifier: letters, digits, '.', '-', '_' (at most 100); not "." or "..".
bool streamIdentifierValid(const char *s);
// The extension of a file KaiPlayer plays from the Archive, in lowercase ("mp3"), or NULL.
const char *streamPlayableExtension(const char *file);
