// A Navidrome (Subsonic) server's audiobooks: the albums of a genre named
// as audiobooks ("Audiobook", "Hörbuch"...) or in a library (music folder)
// named so, each one a book, its songs the parts; and the server's bookmarks,
// so a book left on another player goes on from there. Pure: the answers'
// JSON into books and request parameters; bookscan.c asks the server.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "books.h"
#include "json.h"
#include "subsonic.h"

#define BOOKSUB_KEY_PREFIX "navidrome:"
#define BOOKSUB_LISTS_MAX 16     // Genres and folders looked in.
#define BOOKSUB_ALBUMS_MAX 500   // Books from one server.
#define BOOKSUB_PAGE 500         // Albums asked for at once.

typedef struct {
    char params[SUB_ID_MAX * 2 + 64];  // getAlbumList2's, without size and offset.
} BookSubList;

// getGenres's answer: a list for each genre named as audiobooks. Returns how many were added to out.
size_t bookSubGenreLists(const JsonValue *body, BookSubList *out, size_t max);
// getMusicFolders's answer: a list for each folder (library) named as audiobooks.
size_t bookSubFolderLists(const JsonValue *body, BookSubList *out, size_t max);
// getAlbumList2's answer: its albums' ids (as they came), added to ids (each SUB_ID_MAX) up to max,
// skipping those already there. Returns how many it holds now; *page the albums on this page.
size_t bookSubAlbumIds(const JsonValue *body, char (*ids)[SUB_ID_MAX], size_t count, size_t max, size_t *page);
// getAlbum's answer as a book: its songs in disc and track order. One song is left to probe
// (its chapters are in the file); several are the book's chapters, and it's probed.
bool bookSubAlbum(const JsonValue *body, Book *out);
// getBookmarks's answer: each book of l that holds a bookmarked song gets its newest one.
void bookSubApplyBookmarks(const JsonValue *body, BookList *l);
// The song id of a server location ("sub:/<cover>/<id>.m4b"), percent-encoded, for createBookmark.
bool bookSubSongId(const char *location, char *out, size_t size);
// An ISO 8601 time ("2026-11-25T10:20:30.5Z", "+01:00") in seconds since 1970; 0 if it isn't one.
long long bookSubTime(const char *iso);
