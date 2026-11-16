// The music library as artists, albums and songs, built from the tags of
// the songs in the music folder. Songs without tags are grouped by folder:
// "Artist/Album/song.mp3". Pure data: catalog_scan.c fills it on a thread.
#pragma once

#include <stdbool.h>
#include <stddef.h>
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
} Catalog;

void catalogInit(Catalog *c);
void catalogFree(Catalog *c);

// Adds a song from its tags (any may be empty). root is the music folder,
// for folder-based names. False if memory runs out.
bool catalogAdd(Catalog *c, const char *path, long size, const TrackTags *tags, const char *root);
// Adds a song with its fields already worked out (from the cache file).
bool catalogAddFields(Catalog *c, const CatalogSong *fields);

// Sorts the songs and groups them into albums and artists. Call after the
// last catalogAdd; the catalog can be read once this returns true.
bool catalogBuild(Catalog *c);

// The song with this path, or -1. Needs catalogBuild.
long catalogFind(const Catalog *c, const char *path);

// The cache file: one song per line. Load fills an unbuilt catalog.
bool catalogSave(const Catalog *c, FILE *f);
bool catalogLoad(Catalog *c, FILE *f);
