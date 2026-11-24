// Synced lyrics: a .lrc file next to the song ("Song.flac" -> "Song.lrc"),
// lines with the time they are sung ("[01:23.45]words"). Pure parsing,
// tested on the PC; artwork.c reads the file with the song's cover.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define LYRICS_FILE_MAX (256 * 1024)  // A bigger "lyrics" file isn't one.
#define LYRICS_LINES_MAX 2000

typedef struct {
    int ms;            // When the line starts, from the start of the song.
    const char *text;  // In Lyrics.buffer; may be empty (a pause between verses).
} LyricLine;

typedef struct {
    LyricLine *lines;  // In time order.
    int count;
    char *buffer;
} Lyrics;

// Reads an LRC file's text. Timed lines only (a file without times isn't
// synced lyrics); a line with several times ("[00:12][01:40]chorus") is
// there at each; [offset:+/-ms] moves them all; tags ([ar:], [ti:]) and
// word times ("<00:12.30>") are left out. False, with nothing kept, if
// there's no timed line. Free with lyricsFree.
bool lyricsParse(const char *text, size_t size, Lyrics *out);
void lyricsFree(Lyrics *l);

// The line being sung at seconds: the last one started by then; -1 before the first.
int lyricsLineAt(const Lyrics *l, double seconds);

// The .lrc next to a song: its path with the extension changed. False if it doesn't fit.
bool lyricsPathFor(const char *song, char *out, size_t size);
