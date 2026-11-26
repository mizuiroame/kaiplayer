// Online music: Jamendo (Creative Commons music, its API v3.0 with a client ID) and the Internet
// Archive (the Live Music Archive, and audio with a Creative Commons or public-domain license; no key).
// They are browsed, not read whole as Navidrome's library is: each page is one request, its answer
// a list of songs, albums, artists or items (StreamList).
//
// Their songs are locations, as the server's "sub:/" ones, which kfile.c reads through streamfile.c:
//   jam:/<cover>/<license>/<track id>.mp3    cover: the album's id, "a<id>" for an artist's, or "-"
//   ia:/<identifier>/<license>/<file>        file: its name in the item, percent-encoded, with its
//                                            extension (.mp3, .ogg, .flac) left at the end
// and their covers "jam:/<cover>/cover/<px>.jpg" and "ia:/<identifier>/cover/<px>.jpg". The license
// code ("by-nc-sa-3.0", "cc0-1.0", "pd", "lma" for the Live Music Archive, "-" for none said) is in
// the location, so a playlist's song still shows it after a restart.
//
// stream.c is pure: locations, licenses, the requests' URLs, and the answers (JSON) into lists. It
// takes untrusted answers: every text is bounded, and anything unexpected is left out.
// streamnet.c asks the services.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "json.h"
#include "util.h"

#define STREAM_JAMENDO_PREFIX "jam:/"
#define STREAM_ARCHIVE_PREFIX "ia:/"
#define STREAM_TEXT_MAX 192        // A title, an artist or an album, in UTF-8 (cut at a character).
#define STREAM_ID_MAX 104          // A Jamendo id, or an Archive identifier (at most 100 characters).
#define STREAM_FILE_MAX 320        // An Archive file's name.
#define STREAM_LICENSE_MAX 24      // A license code.
#define STREAM_DETAIL_MAX 48       // A year or a date.
#define STREAM_CLIENT_ID_MAX 64    // A Jamendo client ID (8 hex digits, so far).
#define STREAM_WORDS_MAX 128       // What was typed to search for.
#define STREAM_PAGE_SIZE 50        // Results asked for on a page.
#define STREAM_ITEMS_MAX 500       // An Archive item's songs, at most.
#define STREAM_URL_MAX 2048
#define STREAM_COVER_PX_MAX 600    // Jamendo's largest cover.

typedef enum {
    STREAM_JAMENDO,
    STREAM_ARCHIVE,
    STREAM_SERVICE_COUNT,
} StreamService;

// True for "jam:/..." or "ia:/..." locations.
bool streamIsLocation(const char *path);

// --- Locations ---

typedef struct {
    StreamService service;
    bool cover;                       // A cover's location, at px pixels.
    int px;
    char key[STREAM_ID_MAX];          // Jamendo: the cover's key ("123", "a45", "-"); the Archive: the identifier.
    char license[STREAM_LICENSE_MAX]; // A song's license code.
    char id[STREAM_ID_MAX];           // Jamendo: the track's id.
    char file[STREAM_FILE_MAX];       // The Archive: the file's name in the item, decoded.
} StreamTarget;

// A Jamendo song's location: album_id ("" for a single) for its cover, license_url as Jamendo gives it.
// False if an id isn't digits, or it doesn't fit.
bool streamJamendoLocation(const char *album_id, const char *license_url, const char *track_id, char *out,
                           size_t size);
// An Archive song's location. license: a code (streamLicenseCode, or "lma"). False if the identifier
// or the file can't be one (characters an identifier can't have, a file KaiPlayer can't play).
bool streamArchiveLocation(const char *identifier, const char *license, const char *file, char *out, size_t size);
// A cover's location for a Jamendo album or artist (artist: "a" + id), or an Archive item.
bool streamCoverKeyLocation(StreamService service, const char *key, char *out, size_t size);
// The cover location of a song's (or of a cover's location) at px pixels. False if it has none.
bool streamCoverLocation(const char *location, int px, char *out, size_t size);
// What a location names. False if it isn't a song's or a cover's.
bool streamParseLocation(const char *location, StreamTarget *out);

// --- Licenses and credits ---

// "http://creativecommons.org/licenses/by-nc-sa/3.0/" -> "by-nc-sa-3.0"; ".../publicdomain/zero/1.0/"
// -> "cc0-1.0"; ".../publicdomain/mark/1.0/" -> "pdm-1.0"; ".../licenses/publicdomain/" -> "pd".
// Anything else (or nothing) -> "-".
void streamLicenseCode(const char *license_url, char *out, size_t size);
// Its name to show: "CC BY-NC-SA 3.0", "CC0 1.0", "Public domain", "Live Music Archive · non-commercial";
// "" for "-".
void streamLicenseName(const char *code, char *out, size_t size);
// Where its text is, without "https://": "creativecommons.org/licenses/by-nc-sa/3.0"; "" for none.
void streamLicenseLink(const char *code, char *out, size_t size);
// "Jamendo" or "Internet Archive".
const char *streamServiceName(StreamService service);
// A song's page, without "https://": "jamendo.com/track/123", "archive.org/details/<identifier>".
bool streamPageLink(const char *location, char *out, size_t size);
// The credit line for a song: "CC BY-NC-SA 3.0 · Jamendo · jamendo.com/track/123". False if it isn't one.
bool streamCreditLine(const char *location, char *out, size_t size);

// --- Requests ---

typedef enum {
    STREAM_Q_JAM_POPULAR,          // Songs, most listened to this month.
    STREAM_Q_JAM_NEW_ALBUMS,
    STREAM_Q_JAM_GENRE,            // Songs with the tag in words.
    STREAM_Q_JAM_SEARCH_TRACKS,
    STREAM_Q_JAM_SEARCH_ALBUMS,
    STREAM_Q_JAM_SEARCH_ARTISTS,
    STREAM_Q_JAM_ARTIST_ALBUMS,    // id: the artist's.
    STREAM_Q_JAM_ALBUM_TRACKS,     // id: the album's.
    STREAM_Q_JAM_TEST,             // One song: the client ID works.
    STREAM_Q_JAM_AUDIO,            // id: a track's; its audio's address.
    STREAM_Q_IA_POPULAR_LIVE,      // Live Music Archive concerts, most downloaded.
    STREAM_Q_IA_SEARCH_LIVE,
    STREAM_Q_IA_POPULAR_NETLABELS, // Netlabels' releases with a Creative Commons license.
    STREAM_Q_IA_SEARCH_FREE,       // Audio with a Creative Commons or public-domain license.
    STREAM_Q_IA_ITEM,              // id: an item's identifier; its songs.
    STREAM_Q_IA_TEST,              // How many live concerts there are.
    STREAM_Q_COUNT,
} StreamQueryKind;

typedef struct {
    StreamQueryKind kind;
    char words[STREAM_WORDS_MAX];  // What to search for, or a genre's tag.
    char id[STREAM_ID_MAX];
} StreamQuery;

StreamService streamQueryService(StreamQueryKind kind);
// True if a Jamendo client ID can be this (letters and digits, at most STREAM_CLIENT_ID_MAX - 1).
bool streamClientIdValid(const char *client_id);
// The request's URL (Jamendo's with client_id). False if something in it can't be used (no words to
// search for, an id that isn't one, an invalid client ID) or it doesn't fit.
bool streamQueryUrl(const StreamQuery *q, const char *client_id, char *out, size_t size);
// An Archive file's address, to read it: https://archive.org/download/<identifier>/<file, encoded>.
bool streamArchiveFileUrl(const char *identifier, const char *file, char *out, size_t size);
// A cover location's address (Jamendo's usercontent, the Archive's item image). False for no cover.
bool streamCoverUrl(const StreamTarget *cover, char *out, size_t size);

// --- Answers ---

typedef enum {
    STREAM_ITEM_TRACK,    // A song: location plays it.
    STREAM_ITEM_ALBUM,    // A Jamendo album: id.
    STREAM_ITEM_ARTIST,   // A Jamendo artist: id.
    STREAM_ITEM_ARCHIVE,  // An Archive item (a concert, a release): id is its identifier.
} StreamItemKind;

typedef struct {
    StreamItemKind kind;
    char id[STREAM_ID_MAX];
    char title[STREAM_TEXT_MAX];
    char artist[STREAM_TEXT_MAX];
    char album[STREAM_TEXT_MAX];
    char detail[STREAM_DETAIL_MAX];  // A year ("2019"), or a date ("1977-05-08").
    int position;                    // A song's number in its album, or 0.
    // A song's location; for the others, their cover's (streamCoverKeyLocation), or "".
    char location[UTIL_PATH_MAX];
} StreamItem;

typedef struct {
    StreamItem *items;
    size_t count, capacity;
    long total;   // Results there are in all, when the service says (-1 if not).
    int number;   // STREAM_Q_IA_TEST: the concerts found.
} StreamList;

void streamListFree(StreamList *list);

// The answer to q in root, into out (emptied first). False with why (for the person) if it says it
// failed, isn't the answer expected, or memory ran out. An answer with nothing in it is true and empty.
bool streamParseAnswer(const StreamQuery *q, const JsonValue *root, StreamList *out, char *why, size_t why_size);
// STREAM_Q_JAM_AUDIO's answer: the song's audio address (https://...). False if there's none.
bool streamJamendoAudioUrl(const JsonValue *root, char *out, size_t size);

// The words typed for an Archive search, as search terms: letters, digits and the characters of
// other alphabets kept (ASCII in lowercase, so AND/OR/NOT aren't operators), anything else a space,
// spaces squeezed. False if nothing is left.
bool streamArchiveWords(const char *typed, char *out, size_t size);

// The genres the Jamendo page offers: their tags, and their names to show (N_ texts).
#define STREAM_GENRE_COUNT 16
const char *streamGenreTag(int index);
const char *streamGenreName(int index);
