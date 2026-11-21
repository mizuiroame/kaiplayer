// The Navidrome (Subsonic) server (subsonic.h): its account, kept for every
// thread, and what's asked of it. The console's network must be started
// (netInit) before any of these is called.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "catalog.h"
#include "json.h"
#include "net.h"
#include "subsonic.h"

// The account in use (copied). An empty address: no server.
void subUseAccount(const SubAccount *a);
void subGetAccount(SubAccount *out);
// There is a server's address (signed in or not).
bool subConfigured(void);
// And a user signed in to it: its library can be read.
bool subSignedInNow(void);

// The account's file in the config folder: read at start, written on a change.
void subLoadAccount(const char *path);
bool subSaveAccount(const char *path);

// Asks the server for method (params already encoded, or NULL) on client (NULL: one of its own). The
// answer, with *body its "subsonic-response", or NULL with why (for the person). Free it with jsonFree.
JsonDoc *subCall(NetClient *client, const char *method, const char *params, const JsonValue **body,
                 const volatile bool *cancel, char *why, size_t why_size);

// Signs in and says what the server is, and how many songs it has, as a sentence. True if it answered.
bool subTest(char *out, size_t size);

// Stops a long fetch when it returns true.
typedef bool (*SubStop)(void);
// Songs read so far, of total (0 if the server doesn't say).
typedef void (*SubProgress)(size_t done, size_t total, void *user);

// The whole library into c (not built), page by page: search3's, or album by album for servers whose
// search3 needs words. False with why if it couldn't be read, or stop said so (why is then "").
bool subFetchLibrary(Catalog *c, SubStop stop, SubProgress progress, void *user, char *why, size_t why_size);

// Tells the server a song is playing (submission false: "now playing"), or was played (true).
bool subScrobble(const char *location, bool submission);

// A location's URL for reading it with ranges ("download", or "stream" with format=raw when downloads are
// off), and whether its certificate is checked. False if there's no server, or it isn't a song's.
bool subSongUrl(const char *location, bool raw_stream, char *url, size_t size, bool *any_certificate);
// A cover's URL ("getCoverArt"). False if there's no server, or it isn't a cover's location.
bool subCoverUrl(const char *location, char *url, size_t size, bool *any_certificate);
