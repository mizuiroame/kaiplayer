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
#define SUB_KEY_MAX 256          // An API key (OpenSubsonic's apiKeyAuthentication; Navidrome's are 26 letters).
#define SUB_API_KEY_EXTENSION "apiKeyAuthentication"
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
    // Or an API key, made in the server's web page: when there is one, it alone signs in (no salt or
    // token), and user is only the name the server said it belongs to (tokenInfo), shown, never sent.
    // It is kept as typed, as the token is: it's what the server asks for.
    char api_key[SUB_KEY_MAX];
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
// Where a server sent the request to address on its way: Navidrome's own page
// under a path ("http://pc:4533/navidrome/app/", its BaseUrl when the address
// was typed without it) -> that address ("http://pc:4533/navidrome"). False
// for anything else, or when it is the address itself.
bool subPointedAddress(const char *address, const char *redirected, char *out, size_t size);

// The sign-in for password with this salt (some random letters): a->salt and a->token.
// False if the salt is unusable (empty, too long, or with characters a URL would change).
bool subSetPassword(SubAccount *a, const char *password, const char *salt);
// Signs in with an API key instead (typed: spaces around it are left out): a->api_key, and no user,
// salt or token. "" signs out. False if it can't be one (too long, spaces or control characters in it).
bool subSetApiKey(SubAccount *a, const char *typed);
// True when there's an address and a user signed in (by a password's token, or an API key).
bool subSignedIn(const SubAccount *a);
// True when the sign-in is an API key.
bool subUsesApiKey(const SubAccount *a);

// The account's file: "address=", "user=", "salt=", "token=", "api_key=", "check_certificate=" lines.
void subAccountRead(SubAccount *a, const char *text);
bool subAccountWrite(const SubAccount *a, char *out, size_t size);

// Percent-encodes everything but letters, digits and "-._~". False if it doesn't fit.
bool subEncode(const char *text, char *out, size_t size);
// The URL of a request: address/rest/method?u=...&t=...&s=...&v=...&c=...&f=json (with an API key,
// ?apiKey=...&v=..., no user: the server takes no other sign-in with it), then "&" and params (already
// encoded; may be NULL). False if it doesn't fit.
bool subRequestUrl(const SubAccount *a, const char *method, const char *params, char *out, size_t size);
// The URL of a request without any sign-in (getOpenSubsonicExtensions'): address/rest/method?v=...&c=...&f=json.
bool subPublicUrl(const char *address, const char *method, char *out, size_t size);

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

// getLyricsBySongId's answer (OpenSubsonic) as LRC text in out: the first
// synced lyrics as "[mm:ss.xx]line" lines, or else the first plain ones as
// lines. False if it has none, or they don't fit in size.
bool subLyricsText(const JsonValue *body, char *out, size_t size);

// getOpenSubsonicExtensions' answer: true if it lists the extension name.
bool subHasExtension(const JsonValue *body, const char *name);
// tokenInfo's answer (an API key's): the user it belongs to, in out. False if it doesn't say.
bool subTokenUser(const JsonValue *body, char *out, size_t size);

// scanStatus's count of songs; -1 if it doesn't say.
long subScanCount(const JsonValue *body);
