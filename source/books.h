// Audiobooks: what a book is, which files make one, and where in a book a
// time is. A book is a single .m4b (or one file in a folder of books), or a
// folder of files played in order ("CD 1", "CD 2" folders under it count as
// one book), or an album of a Navidrome server. Pure: bookscan.c finds the
// files and reads their lengths and chapters on a thread; the UI reads the list.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "bookchap.h"

#define BOOK_TEXT_MAX 256
#define BOOKS_MAX 2000
#define BOOK_PARTS_MAX 1000
// An MP3 without a length in its header: its length from its size at this bitrate.
#define BOOK_GUESS_BYTES_PER_SECOND 16000.0

typedef struct {
    char *path;          // Where it is: card, network folder ("dav:/") or server ("sub:/").
    long size;           // Bytes, to notice a changed file.
    double duration;     // Seconds; 0 until read.
} BookPart;

typedef struct {
    char *key;                     // The folder, the file, or "navidrome:<album id>": stays the same.
    char title[BOOK_TEXT_MAX];
    char author[BOOK_TEXT_MAX];
    BookPart *parts;               // In playing order.
    size_t part_count;
    BookChapters chapters;         // Times from the book's start; at least one once probed.
    double duration;               // Sum of the parts'.
    bool probed;                   // Lengths, chapters and names have been read.
    bool server;                   // A Navidrome album: lengths and names came with it.
    // The server's newest bookmark in it (Navidrome): mark_time 0 if none.
    size_t mark_part;
    double mark_position;
    long long mark_time;
} Book;

typedef struct {
    Book *items;
    size_t count, capacity;
    unsigned generation;  // Set by whoever publishes it (bookscan.c): another list, or a book changed.
} BookList;

void bookFree(Book *b);
void bookListFree(BookList *l);
// A deep copy. False (out empty) if memory runs out.
bool bookCopy(Book *out, const Book *in);
// Takes b's contents (b is left empty). False if memory runs out or the list is full.
bool bookListTake(BookList *l, Book *b);
// Adds a part (copying path). False if memory runs out.
bool bookAddPart(Book *b, const char *path, long size, double duration);
// The book with this key, or NULL.
const Book *booksFind(const BookList *l, const char *key);
// Same files, same sizes, in the same order: what was read of one holds for the other.
bool bookSameParts(const Book *a, const Book *b);

// A folder's or a genre's name that says audiobooks: "Audiobooks", "Hörbücher", "Audiolibros"...
bool booksIsBookName(const char *name);
// A file the Audiobooks tab takes from a library: an .m4b, or one inside a folder with such a name.
bool booksIsBookPath(const char *path);
// "CD 1", "Disc02", "Part 3": a folder of a book in parts.
bool booksIsDiscFolder(const char *name);
// Names compared with their numbers as numbers: "Chapter 2" before "Chapter 10".
int booksCompareNatural(const char *a, const char *b);

// Groups files into books (not probed): each .m4b alone; the others by their
// folder (a disc folder's by the one above it); files right in a folder named
// as a books' folder alone. The books are added to out.
bool booksGroup(BookList *out, const char *const *paths, const long *sizes, size_t count);
// Sorts by title.
void booksSortByTitle(BookList *l);

// After the parts' lengths and the chapters were read: the book's length, and
// its chapters in order and named; a book without any gets one per part, named
// part_titles[i] when given (may be NULL, or hold "") or after the file.
void bookFinishProbe(Book *b, const char *const *part_titles);

// Seconds into the book where part starts, and of a position in a part.
double bookPartStart(const Book *b, size_t part);
double bookTimeAt(const Book *b, size_t part, double position);
// The part and the position in it of seconds into the book (kept inside it).
void bookLocate(const Book *b, double seconds, size_t *part, double *position);
// The chapter at seconds, and where it ends (the book's end for the last).
size_t bookChapterIndex(const Book *b, double seconds);
double bookChapterEnd(const Book *b, size_t chapter);

// The list kept between runs (books.cache): what was read of each book.
bool booksSave(const BookList *l, FILE *f);
bool booksLoad(BookList *l, FILE *f);
