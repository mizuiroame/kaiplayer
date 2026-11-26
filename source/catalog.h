// The music library as artists, albums and songs, built from the tags of
// the songs in the music folder. Songs without tags are grouped by folder:
// "Artist/Album/song.mp3". Pure data: catalog_scan.c fills it on a thread.
// The folder's audiobooks (booksIsBookPath: an .m4b, or under a folder named
// "Audiobooks"...) are kept apart, in books: they aren't songs, albums or
// artists, but the Audiobooks tab finds them here (bookscan.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "tags.h"

#define CATALOG_UNKNOWN_ARTIST "Unknown artist"

typedef struct {
    char *path;
    char *title;          // Tag title, or the file name without extension.
    char *artist;         // Song artist (shown in song rows).
    char *album_artist;   // Groups the album: album artist, else artist, else folder.
    char *album;          // Album title, else the folder name.
    int track, disc, year;
    long size;            // File size, to notice changed files on rescans.
    int64_t modified;     // And the file's time (seconds since 1970); 0 if unknown.
    size_t album_index;   // Set by catalogBuild.
} CatalogSong;

typedef struct {
    char *title;
    size_t artist;        // Index in artists.
    size_t *songs;        // Song indexes, by disc, track and title.
    size_t song_count;
    int year;
} CatalogAlbum;

typedef struct {
    char *name;
    size_t *albums;       // Album indexes, by year and title.
    size_t album_count;
    size_t song_count;
} CatalogArtist;

typedef struct {
    CatalogSong *songs;   // Sorted by path (catalogFind uses this).
    size_t song_count, song_capacity;
    CatalogAlbum *albums; // Sorted by title.
    size_t album_count;
    CatalogArtist *artists;  // Sorted by name.
    size_t artist_count;
    size_t *songs_by_title;  // Song indexes for the "Songs" list.
    CatalogSong *books;      // The audiobooks' files: path, size and time only. Sorted by path.
    size_t book_count, book_capacity;
    unsigned generation;     // Set when published; indexes from another generation are stale.
} Catalog;

void catalogInit(Catalog *c);
void catalogFree(Catalog *c);

// Adds a song from its tags (any may be empty). root is the music folder,
// for folder-based names. False if memory runs out.
bool catalogAdd(Catalog *c, const char *path, long size, const TrackTags *tags, const char *root);
// The same with the file's time, as a network folder's listing gives it.
bool catalogAddFile(Catalog *c, const char *path, long size, int64_t modified, const TrackTags *tags,
                    const char *root);
// Adds a song with its fields already worked out (from the cache file).
// An audiobook's file (booksIsBookPath) goes to books instead, whatever it's added with.
bool catalogAddFields(Catalog *c, const CatalogSong *fields);
// Adds an audiobook's file to books, with no tags (the Audiobooks tab reads its own).
bool catalogAddBook(Catalog *c, const char *path, long size, int64_t modified);
// The book's file with this path in books, or -1. Needs catalogBuild.
long catalogFindBook(const Catalog *c, const char *path);

// Whether a library's file times can be trusted to notice changes: some are
// set (not 0) and they aren't all the same (a card whose times all read the
// same tells nothing). Without them, files are compared by size alone.
bool catalogTimesUsable(const int64_t *times, size_t count);
// Whether a file found again is the song known before, with no need to read
// its tags: the same size and, when modified is known (not 0), the same time.
// A song kept without a time (before times could be trusted) is read once more.
bool catalogSongUnchanged(const CatalogSong *known, long size, int64_t modified);

// Sorts the songs and groups them into albums and artists. Call after the
// last catalogAdd; the catalog can be read once this returns true.
bool catalogBuild(Catalog *c);

// The song with this path, or -1. Needs catalogBuild.
long catalogFind(const Catalog *c, const char *path);

// The cache file: one song per line, the books' files after them. Load fills
// an unbuilt catalog (and reads the files of KaiPlayer 1.1, without the times);
// the books in a file of KaiPlayer 1.7, kept there as songs, go to books.
bool catalogSave(const Catalog *c, FILE *f);
bool catalogLoad(Catalog *c, FILE *f);
