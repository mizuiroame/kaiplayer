// Plain (ASCII) file names that the Switch 2 can list. The console's file
// system leaves names with other characters (accents, Japanese) out of its
// folders, so a name the user sees can be anything, but the file gets one of
// these: "Español" -> "Espanol", "松原みき" -> "Playlist".
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Accents dropped, typographic quotes and dashes made plain, other
// characters as "_" (runs collapsed); no leading or trailing spaces or dots.
// `fallback` when nothing readable is left. At most size - 1 characters.
void plainName(const char *utf8, const char *fallback, char *out, size_t size);

// True if every character is one plainName() keeps.
bool plainNameIs(const char *name);
