// Online music from the Internet Archive (archive.org): the Live Music Archive's concerts, 78 rpm records,
// public-domain classical music, and audio with a Creative Commons or public-domain license. No key is
// needed. It is browsed, not read whole as Navidrome's library is: each page is one request, its answer
// a list of items (a concert, a record, an album) or of an item's songs (StreamList).
//
// Its songs are locations, as the server's "sub:/" ones, which kfile.c reads through streamfile.c:
//   ia:/<identifier>/<license>/<file>   file: its name in the item, percent-encoded, with its extension
//                                       (.mp3, .ogg, .flac) left at the end
// and an item's cover "ia:/<identifier>/cover/<px>.jpg". The license code ("by-nc-sa-3.0", "cc0-1.0",
// "pd", "lma" for the Live Music Archive, "78rpm" for the 78 rpm collection, "-" for none said) is in the
// location, so a playlist's song still shows it after a restart.
//
// stream.c is pure: locations, licenses, the requests' URLs; streamparse.c reads the answers (JSON) into
// lists. They take untrusted answers: every text is bounded, and anything unexpected is left out.
// streamnet.c asks the Archive.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "json.h"
#include "util.h"

#define STREAM_PREFIX "ia:/"
#define STREAM_TEXT_MAX 192        // A title, a creator or an album, in UTF-8 (cut at a character).
#define STREAM_ID_MAX 104          // An Archive identifier (at most 100 characters).
#define STREAM_FILE_MAX 320        // A file's name in its item.
#define STREAM_LICENSE_MAX 24      // A license code.
#define STREAM_DETAIL_MAX 48       // A date ("1977-05-08") or a year.
#define STREAM_WORDS_MAX 128       // What was typed to search for.
#define STREAM_PAGE_SIZE 50        // Results asked for on a page.
#define STREAM_ITEMS_MAX 500       // An item's songs, at most.
#define STREAM_URL_MAX 2048

// True for "ia:/..." locations.
bool streamIsLocation(const char *path);

// --- Locations ---

typedef struct {
    bool cover;                       // An item's cover, at px pixels.
    int px;
    char identifier[STREAM_ID_MAX];
    char license[STREAM_LICENSE_MAX]; // A song's license code.
    char file[STREAM_FILE_MAX];       // A song's file in the item, decoded.
} StreamTarget;

// A song's location. license: a code (streamLicenseCode, "lma" or "78rpm"). False if the identifier or
// the file can't be one (characters an identifier can't have, a file KaiPlayer can't play).
bool streamSongLocation(const char *identifier, const char *license, const char *file, char *out, size_t size);
// An item's cover location (px 0: the key a row shows it by).
bool streamItemCoverLocation(const char *identifier, char *out, size_t size);
// The cover location of a song's item (or of a cover's location) at px pixels.
bool streamCoverLocation(const char *location, int px, char *out, size_t size);
// What a location names. False if it isn't a song's or a cover's.
bool streamParseLocation(const char *location, StreamTarget *out);

// --- Licenses and credits ---

// "http://creativecommons.org/licenses/by-nc-sa/3.0/" -> "by-nc-sa-3.0"; ".../publicdomain/zero/1.0/"
// -> "cc0-1.0"; ".../publicdomain/mark/1.0/" -> "pdm-1.0"; ".../licenses/publicdomain/" -> "pd".
// Anything else (or nothing) -> "-".
void streamLicenseCode(const char *license_url, char *out, size_t size);
// Its name to show: "CC BY-NC-SA 3.0", "CC0 1.0", "Public domain", "Live Music Archive: non-commercial",
// "78 rpm collection"; "" for "-".
void streamLicenseName(const char *code, char *out, size_t size);
// Where its text is, without "https://": "creativecommons.org/licenses/by-nc-sa/3.0"; "" for none.
void streamLicenseLink(const char *code, char *out, size_t size);
// A song's page, without "https://": "archive.org/details/<identifier>".
bool streamPageLink(const char *location, char *out, size_t size);
// The credit line for a song: "CC BY 4.0 · Internet Archive · archive.org/details/<identifier>".
bool streamCreditLine(const char *location, char *out, size_t size);

// --- Requests ---

typedef enum {
    STREAM_Q_POPULAR_LIVE,       // Live Music Archive concerts, most downloaded.
    STREAM_Q_SEARCH_LIVE,
    STREAM_Q_POPULAR_78,         // The 78 rpm collection's records, most downloaded.
    STREAM_Q_SEARCH_78,
    STREAM_Q_CLASSICAL,          // Public-domain classical music, most downloaded.
    STREAM_Q_SEARCH_FREE,        // Audio with a Creative Commons or public-domain license.
    STREAM_Q_ITEM,               // id: an item's identifier; its songs.
    STREAM_Q_COUNT,
} StreamQueryKind;

typedef struct {
    StreamQueryKind kind;
    char words[STREAM_WORDS_MAX];  // What to search for.
    char id[STREAM_ID_MAX];
} StreamQuery;

// The request's URL. False if something in it can't be used (no words to search for, an identifier that
// isn't one) or it doesn't fit.
bool streamQueryUrl(const StreamQuery *q, char *out, size_t size);
// A song's file address, to read it: https://archive.org/download/<identifier>/<file, encoded>.
bool streamFileUrl(const StreamTarget *song, char *out, size_t size);
// A cover location's address (the item's image).
bool streamCoverUrl(const StreamTarget *cover, char *out, size_t size);

// The words typed for a search, as search terms: letters, digits and the characters of other alphabets
// kept (ASCII in lowercase, so AND/OR/NOT aren't operators), anything else a space, spaces squeezed.
// False if nothing is left.
bool streamSearchWords(const char *typed, char *out, size_t size);

// --- Answers ---

typedef enum {
    STREAM_ITEM_TRACK,    // A song: location plays it.
    STREAM_ITEM_ARCHIVE,  // An item (a concert, a record, an album): id is its identifier.
} StreamItemKind;

typedef struct {
    StreamItemKind kind;
    char id[STREAM_ID_MAX];
    char title[STREAM_TEXT_MAX];
    char artist[STREAM_TEXT_MAX];
    char album[STREAM_TEXT_MAX];
    char detail[STREAM_DETAIL_MAX];  // A date ("1977-05-08"), or a year.
    int position;                    // A song's number in its item, or 0.
    // A song's location; for an item, its cover's (streamItemCoverLocation), or "".
    char location[UTIL_PATH_MAX];
} StreamItem;

typedef struct {
    StreamItem *items;
    size_t count, capacity;
    long total;   // Results there are in all, when the Archive says (-1 if not).
} StreamList;

void streamListFree(StreamList *list);

// The answer to q in root, into out (emptied first). False with why (for the person) if it says it
// failed, isn't the answer expected, or memory ran out. An answer with nothing in it is true and empty.
bool streamParseAnswer(const StreamQuery *q, const JsonValue *root, StreamList *out, char *why, size_t why_size);

// The Archive's first page: its rows, in order. A row asks for a query kind, typing words first if it's a
// search.
int streamMenuCount(void);
const char *streamMenuLabel(int row);  // In the language in use.
StreamQueryKind streamMenuQuery(int row);
bool streamMenuSearches(int row);
