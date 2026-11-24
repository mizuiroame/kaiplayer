// Lyrics: a .lrc file next to the song ("Song.flac" -> "Song.lrc"), the
// song's own (ID3 USLT, a Vorbis LYRICS comment) or the server's. Synced
// lyrics have the time each line is sung ("[01:23.45]words"); plain ones are
// only text, which scrolls along with the song. Pure parsing, tested on the
// PC; artwork.c reads them with the song's cover.
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
    LyricLine *lines;  // In time order (synced), or in the text's (plain: their ms is 0).
    int count;
    bool synced;
    char *buffer;
} Lyrics;

// Reads lyrics. With timed lines, those only (synced): a line with several
// times ("[00:12][01:40]chorus") is there at each; [offset:+/-ms] moves them
// all; tags ([ar:], [ti:]) and word times ("<00:12.30>") are left out.
// Without any, plain lyrics: every line but tags, blank lines kept between
// verses (not at the start or end). False, with nothing kept, if there's no
// text at all. Free with lyricsFree.
bool lyricsParse(const char *text, size_t size, Lyrics *out);
void lyricsFree(Lyrics *l);

// The line being sung at seconds: the last one started by then; -1 before the first.
int lyricsLineAt(const Lyrics *l, double seconds);

// Plain lyrics: where the text is at seconds of a song duration long, in
// lines (0 is the first line in the middle, count - 1 the last): it moves on
// evenly through the song. 0 for an unknown duration.
double lyricsScrollAt(const Lyrics *l, double seconds, double duration);

// The .lrc next to a song: its path with the extension changed. False if it doesn't fit.
bool lyricsPathFor(const char *song, char *out, size_t size);
