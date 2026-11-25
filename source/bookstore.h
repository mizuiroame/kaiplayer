// Where each audiobook was left: its file, the position in it, when, and
// whether it was finished. Kept apart from the music's resume (resume.h), in
// books.txt in KaiPlayer's folder, so music played between chapters never
// moves a book's place. Pure: the app reads and writes the file.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define BOOKSTORE_FILE "books.txt"
#define BOOKSTORE_MAX 2000

typedef struct {
    char *key;           // The book's key (books.h).
    size_t part;
    double position;     // Seconds into the part.
    long long updated;   // Seconds since 1970.
    bool finished;
} BookMark;

typedef struct {
    BookMark *items;
    size_t count, capacity;
    char *active;        // The book playing when KaiPlayer closed, or NULL.
    int speed;           // Thousandths (tempo.h); 0 if never set.
} BookStore;

void bookStoreFree(BookStore *s);
const BookMark *bookStoreFind(const BookStore *s, const char *key);
// Sets (or adds) the book's place. False if memory runs out.
bool bookStoreSet(BookStore *s, const char *key, size_t part, double position, long long now, bool finished);
// Forgets a book's place (it starts from the beginning again).
void bookStoreForget(BookStore *s, const char *key);
// The book playing (NULL: none).
bool bookStoreSetActive(BookStore *s, const char *key);

// Lines "mark\t<part>\t<position>\t<updated>\t<finished>\t<key>", "active\t<key>", "speed\t<n>".
bool bookStoreSave(const BookStore *s, FILE *f);
bool bookStoreLoad(BookStore *s, FILE *f);
