// An audiobook's chapters: where each starts in the book, and its title.
// They come from the files (MP4/M4B chapter atoms through FFmpeg, ffaudio.c;
// ID3v2 CHAP frames in MP3), from a .cue sheet beside a file, or are the
// book's files themselves. Pure: the parsers take untrusted bytes and check
// every length.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOK_CHAPTER_TITLE 128
#define BOOK_CHAPTERS_MAX 2000   // More is not a book's chapters.

typedef struct {
    double start;                    // Seconds from the start of the book (or of a file, while read).
    char title[BOOK_CHAPTER_TITLE];  // UTF-8; "" until bookChaptersPrepare names it.
} BookChapter;

typedef struct {
    BookChapter *items;
    size_t count, capacity;
} BookChapters;

void bookChaptersFree(BookChapters *c);
// Appends one (title may be NULL). Past BOOK_CHAPTERS_MAX it's left out and
// still returns true; false means out of memory.
bool bookChaptersAdd(BookChapters *c, double start, const char *title);
// Appends every chapter of src, moved later by offset seconds.
bool bookChaptersAppend(BookChapters *c, const BookChapters *src, double offset);

// Puts the chapters in order of time, drops those starting at the same time
// as the one before, and names the ones without a title ("Chapter 2").
void bookChaptersPrepare(BookChapters *c);
// The chapter playing at seconds (they're in order): 0 before the first.
size_t bookChapterAt(const BookChapters *c, double seconds);

// The CHAP frames of an ID3v2.3 or 2.4 tag (data starts with "ID3"), each
// with its TIT2 title if it has one. False if there are none.
bool bookChaptersParseId3(const uint8_t *data, size_t size, BookChapters *out);
// A cue sheet's tracks (TITLE, INDEX 01 mm:ss:ff) for a single file. False
// if it has none, or names more than one file.
bool bookChaptersParseCue(const char *text, size_t size, BookChapters *out);
