// A Navidrome server, or any other with the Subsonic API (Airsonic, Gonic,
// Ampache's): a source of the library, its songs played from there. Its
// songs are "sub:/<cover>/<id>.<suffix>" locations: the song's id and its
// cover's (or "-"), percent-encoded, and its file's extension, which says
// how to play it. kfile.c reads them in blocks (subfile.c, with HTTP ranges).
//
// subsonic.c is pure: addresses, the account's file, the sign-in's token,
// the requests' URLs, locations, and the answers (JSON) into the catalog.
// subnet.c asks the server.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "catalog.h"
#include "json.h"
#include "md5.h"

#define SUB_PREFIX "sub:/"
#define SUB_ROOT SUB_PREFIX      // The library's "folder", as the catalog has it.
#define SUB_ADDRESS_MAX 256
#define SUB_TEXT_MAX 128         // A user name or a password.
#define SUB_SALT_MAX 33
#define SUB_ID_MAX 192           // An id, percent-encoded.
#define SUB_ACCOUNT_FILE "navidrome.txt"
#define SUB_API_VERSION "1.16.1"
#define SUB_CLIENT "KaiPlayer"
#define SUB_NO_COVER "-"
#define SUB_SONGS_MAX 200000     // A library's songs, at most.

typedef struct {
    // "http://192.168.0.10:4533" or "https://music.example.com/navidrome", no '/' at the end; "" for none.
    char address[SUB_ADDRESS_MAX];
    char user[SUB_TEXT_MAX];
    // The password is never kept: only a salt and md5(password + salt), which is all Subsonic's sign-in needs.
    char salt[SUB_SALT_MAX];
    char token[MD5_HEX_SIZE];
    bool check_certificate;      // Off: an https server's own certificate is taken too.
} SubAccount;

// True for "sub:/..." locations.
bool subIsLocation(const char *path);

// What was typed, as an address: "192.168.0.10:4533" -> "http://192.168.0.10:4533";
// Navidrome's page ("http://pc:4533/app/#/album") -> "http://pc:4533". False if
// it isn't one (another scheme, spaces, nothing after the scheme, "user@").
bool subNormalizeAddress(const char *typed, char *out, size_t size);
// The address shown: without "http://" ("192.168.0.10:4533").
void subShownAddress(const char *address, char *out, size_t size);
// Same scheme, host and port: the sign-in typed for one isn't sent to the other.
bool subSameServer(const char *a, const char *b);

// The sign-in for password with this salt (some random letters): a->salt and a->token.
// False if the salt is unusable (empty, too long, or with characters a URL would change).
bool subSetPassword(SubAccount *a, const char *password, const char *salt);
// True when there's an address and a user signed in.
bool subSignedIn(const SubAccount *a);

// The account's file: "address=", "user=", "salt=", "token=", "check_certificate=" lines.
void subAccountRead(SubAccount *a, const char *text);
bool subAccountWrite(const SubAccount *a, char *out, size_t size);

// Percent-encodes everything but letters, digits and "-._~". False if it doesn't fit.
bool subEncode(const char *text, char *out, size_t size);
// The URL of a request: address/rest/method?u=...&t=...&s=...&v=...&c=...&f=json, then
// "&" and params (already encoded; may be NULL). False if it doesn't fit.
bool subRequestUrl(const SubAccount *a, const char *method, const char *params, char *out, size_t size);

// A song's location from its id, its cover's id ("" for none) and its file's extension. False if one
// can't be used (no id, an extension that isn't letters and digits).
bool subSongLocation(const char *id, const char *cover, const char *suffix, char *out, size_t size);
// The location of a song's cover at px pixels ("sub:/<cover>/cover-300.jpg"). False if it has none.
bool subCoverLocation(const char *song, int px, char *out, size_t size);

typedef struct {
    bool cover;            // A cover's location: id is the cover's, at px pixels.
    char id[SUB_ID_MAX];   // Percent-encoded, as a URL takes it.
    int px;
} SubTarget;

// What a location names. False if it isn't a song's or a cover's.
bool subParseLocation(const char *location, SubTarget *out);

// The Subsonic answer in root: true if its status is "ok", with *body its "subsonic-response".
// Otherwise *code is its error's code (0 if it isn't a Subsonic answer) and message its text.
bool subAnswer(const JsonValue *root, const JsonValue **body, int *code, char *message, size_t message_size);

// The values of parent's member name: an array's items one by one, or the member itself if it is a
// single object (as old servers answer). NULL at the end.
const JsonValue *subFirst(const JsonValue *parent, const char *name);
const JsonValue *subNext(const JsonValue *parent, const char *name, const JsonValue *item);

// Adds the songs of parent's member name (search3's, or an album's "song") to c, those KaiPlayer can play.
// album_artist names their album's artist when the songs don't (getAlbum's). *added counts them.
// Past SUB_SONGS_MAX songs in c, the rest are left out. False if memory runs out.
bool subAddSongs(Catalog *c, const JsonValue *parent, const char *name, const char *album_artist, size_t *added);

// scanStatus's count of songs; -1 if it doesn't say.
long subScanCount(const JsonValue *body);
