#include "subnet.h"

#include "dav.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "lyrics.h"
#include "util.h"

#define URL_MAX 2048
#define ANSWER_MAX_BYTES (16u << 20)  // A page of 500 songs is about 1 MB.
#define CALL_STALL_S 10L
#define CALL_TIMEOUT_S 60L
#define PAGE_SONGS 500
#define PAGE_ALBUMS 500
#define HTTP_BAD_REQUEST 400
#define HTTP_UNAUTHORIZED 401
#define HTTP_NOT_FOUND 404
// Subsonic's error codes.
#define SUB_ERROR_CLIENT_OLD 20
#define SUB_ERROR_SERVER_OLD 30
#define SUB_ERROR_LOGIN 40
#define SUB_ERROR_NO_TOKEN 41
#define SUB_ERROR_MISSING 10       // A required parameter: "u", from a server that doesn't take API keys.
#define SUB_ERROR_NOT_SUPPORTED 42 // OpenSubsonic: this way of signing in isn't taken.
#define SUB_ERROR_BAD_KEY 44       // OpenSubsonic: an API key the server doesn't know.
#define SUB_ERROR_NOT_ALLOWED 50
#define SUB_ERROR_NOT_FOUND 70

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static SubAccount g_account;

void subUseAccount(const SubAccount *a)
{
    pthread_mutex_lock(&g_lock);
    g_account = *a;
    pthread_mutex_unlock(&g_lock);
}

void subGetAccount(SubAccount *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_account;
    pthread_mutex_unlock(&g_lock);
}

bool subConfigured(void)
{
    pthread_mutex_lock(&g_lock);
    bool set = g_account.address[0] != '\0';
    pthread_mutex_unlock(&g_lock);
    return set;
}

bool subSignedInNow(void)
{
    pthread_mutex_lock(&g_lock);
    bool signed_in = subSignedIn(&g_account);
    pthread_mutex_unlock(&g_lock);
    return signed_in;
}

void subLoadAccount(const char *path)
{
    SubAccount a;
    char text[1024] = "";
    FILE *f = fopen(path, "r");
    if (f) {
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        text[n] = '\0';
        fclose(f);
    }
    subAccountRead(&a, text);
    subUseAccount(&a);
    memset(text, 0, sizeof(text));
    memset(&a, 0, sizeof(a));
}

bool subSaveAccount(const char *path)
{
    SubAccount a;
    char text[1024];
    subGetAccount(&a);
    bool ok = subAccountWrite(&a, text, sizeof(text));
    FILE *f = ok ? fopen(path, "w") : NULL;
    ok = f && fputs(text, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    memset(text, 0, sizeof(text));
    memset(&a, 0, sizeof(a));
    return ok;
}

// --- Asking ---

// What a connection's failure means, for the person. any_certificate: the
// certificate check was off, so the console's TLS reporting a certificate
// means the handshake itself failed (a plain http server at an https address).
static const char *problemText(NetProblem problem, bool any_certificate)
{
    if (problem == NET_CERTIFICATE && any_certificate)
        problem = NET_SECURE;
    switch (problem) {
    case NET_NO_NAME: return T("The name in the address couldn't be found. Check it, or use the server's IP address.");
    case NET_NO_CONNECT:
        return T("Nothing answered at that address and port. Is the server on, and is the port right (usually 4533)?");
    case NET_TIMEOUT: return T("The server didn't answer in time. Is the console on the same network as the server?");
    case NET_SECURE: return T("The secure connection failed. Does the server use https on that port?");
    case NET_CERTIFICATE:
        return T("The server's certificate isn't trusted. "
                 "If the server has its own certificate, turn Certificate check off.");
    default: return T("The connection failed. Check the address and the network.");
    }
}

// A Subsonic error, for the person. with_key: the sign-in was an API key.
static void errorText(int code, const char *message, bool with_key, char *why, size_t why_size)
{
    if (with_key && (code == SUB_ERROR_MISSING || code == SUB_ERROR_NOT_SUPPORTED)) {
        snprintf(why, why_size, "%s", T("This server doesn't take API keys. Sign in with your user name and password."));
        return;
    }
    switch (code) {
    case SUB_ERROR_BAD_KEY:
        snprintf(why, why_size, "%s",
                 T("The server doesn't know this API key: it may have been revoked. Make a new one in its web page."));
        return;
    case SUB_ERROR_NOT_SUPPORTED:
        snprintf(why, why_size, "%s",
                 T("This server doesn't take a password from KaiPlayer: make an API key in its web page and enter it in API key."));
        return;
    case SUB_ERROR_LOGIN: snprintf(why, why_size, "%s", T("Wrong user name or password.")); return;
    case SUB_ERROR_NO_TOKEN:
        snprintf(why, why_size, "%s", T("This server doesn't take KaiPlayer's way of signing in (a token)."));
        return;
    case SUB_ERROR_NOT_ALLOWED: snprintf(why, why_size, "%s", T("The server doesn't let this user do that.")); return;
    case SUB_ERROR_CLIENT_OLD:
    case SUB_ERROR_SERVER_OLD:
        snprintf(why, why_size, "%s", T("This server's version of the Subsonic API is too old for KaiPlayer."));
        return;
    default: snprintf(why, why_size, T("The server said: %.100s"), message[0] ? message : "?"); return;
    }
}

// The answer's HTTP side, for the person: an answer that isn't a Subsonic server's.
static void statusText(bool plain_http, long status, char *why, size_t why_size)
{
    if (status == HTTP_BAD_REQUEST && plain_http)  // Plain http at an https port.
        snprintf(why, why_size, "%s", T("That port may want a secure connection: write https:// before the address."));
    else if (status == HTTP_UNAUTHORIZED)
        snprintf(why, why_size, "%s", T("Wrong user name or password."));
    else if (status == HTTP_NOT_FOUND)  // Navidrome under a path (its BaseUrl, a proxy's folder) typed without it.
        snprintf(why, why_size, "%s",
                 T("No Navidrome or Subsonic server there. If it's under a path (like /navidrome), add it to the address."));
    else
        snprintf(why, why_size, "%s", T("That address answers, but not as a Navidrome or Subsonic server. Check the port (usually 4533)."));
}

// An answer that isn't a Subsonic server's: where the server pointed, when it
// sent the request to Navidrome's page under a path (its BaseUrl, left out of
// the address); otherwise what its HTTP side says.
static void notSubsonic(const NetResponse *r, const char *address, bool plain_http, long status, char *why,
                        size_t why_size)
{
    char pointed[SUB_ADDRESS_MAX], shown[SUB_ADDRESS_MAX];
    if (subPointedAddress(address, r->redirected, pointed, sizeof(pointed))) {
        subShownAddress(pointed, shown, sizeof(shown));
        snprintf(why, why_size, T("No Navidrome or Subsonic server there: it points to %.200s. Use that address."),
                 shown);
    } else {
        statusText(plain_http, status, why, why_size);
    }
}

// subCall, with *error the Subsonic error's code when the server answered with one (0 otherwise).
static JsonDoc *call(NetClient *client, const char *method, const char *params, const JsonValue **body,
                     const volatile bool *cancel, char *why, size_t why_size, int *error)
{
    *error = 0;
    SubAccount a;
    subGetAccount(&a);
    char url[URL_MAX];
    *body = NULL;
    snprintf(why, why_size, "%s", "");
    if (!subSignedIn(&a) || !subRequestUrl(&a, method, params, url, sizeof(url))) {
        snprintf(why, why_size, "%s", T("Sign in to the Navidrome server in Settings first."));
        memset(&a, 0, sizeof(a));
        return NULL;
    }
    bool plain_http = !strncmp(a.address, "http://", 7);
    bool with_key = subUsesApiKey(&a);
    char address[SUB_ADDRESS_MAX];
    snprintf(address, sizeof(address), "%s", a.address);
    NetClient *own = client ? NULL : netClientOpen();
    NetRequest request = {.url = url, .cancel = cancel, .stall_s = CALL_STALL_S, .timeout_s = CALL_TIMEOUT_S,
                          .accept_json = true, .any_certificate = !a.check_certificate};
    memset(&a, 0, sizeof(a));
    NetResponse response;
    memset(&response, 0, sizeof(response));
    bool fetched = (client || own) && netFetchText(client ? client : own, &request, ANSWER_MAX_BYTES, &response);
    netClientClose(own);
    memset(url, 0, sizeof(url));  // It holds the sign-in's token.
    JsonDoc *doc = NULL;
    if (!client && !own)
        snprintf(why, why_size, "%s", T("The network can't be used."));
    else if (!fetched && cancel && *cancel)
        ;  // Called off: nothing to say.
    else if (!fetched && response.problem == NET_STATUS)
        notSubsonic(&response, address, plain_http, response.status, why, why_size);
    else if (!fetched)
        snprintf(why, why_size, "%s", problemText(response.problem, request.any_certificate));
    else if (!(doc = jsonParse(response.data, response.size)))
        notSubsonic(&response, address, plain_http, 0, why, why_size);
    netResponseFree(&response);
    if (!doc)
        return NULL;
    int code;
    char message[160];
    if (!subAnswer(jsonRoot(doc), body, &code, message, sizeof(message))) {
        if (*body) {
            errorText(code, message, with_key, why, why_size);
            *error = code;
        }
        else
            statusText(plain_http, 0, why, why_size);
        jsonFree(doc);
        *body = NULL;
        return NULL;
    }
    return doc;
}

JsonDoc *subCall(NetClient *client, const char *method, const char *params, const JsonValue **body,
                 const volatile bool *cancel, char *why, size_t why_size)
{
    int error;
    return call(client, method, params, body, cancel, why, why_size, &error);
}

// Whether the server lists API keys among its OpenSubsonic extensions (asked without signing in, as
// getOpenSubsonicExtensions is): 1 yes, 0 no, -1 it couldn't be asked.
static int offersApiKeys(void)
{
    SubAccount a;
    subGetAccount(&a);
    char url[URL_MAX];
    bool ok = subPublicUrl(a.address, "getOpenSubsonicExtensions", url, sizeof(url));
    NetRequest request = {.url = url, .stall_s = CALL_STALL_S, .timeout_s = CALL_TIMEOUT_S, .accept_json = true,
                          .any_certificate = !a.check_certificate};
    memset(&a, 0, sizeof(a));
    NetClient *client = ok ? netClientOpen() : NULL;
    NetResponse response;
    memset(&response, 0, sizeof(response));
    bool fetched = client && netFetchText(client, &request, ANSWER_MAX_BYTES, &response);
    netClientClose(client);
    JsonDoc *doc = fetched ? jsonParse(response.data, response.size) : NULL;
    netResponseFree(&response);
    const JsonValue *body;
    int code, offers = -1;
    char message[160];
    if (doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)))
        offers = subHasExtension(body, SUB_API_KEY_EXTENSION) ? 1 : 0;
    else if (doc && body)
        offers = 0;  // A Subsonic server without OpenSubsonic's extensions.
    jsonFree(doc);
    return offers;
}

// The user an API key belongs to (tokenInfo), kept in the account in use for showing it, if that is
// still the same key. False if the server doesn't say.
static bool learnKeyUser(char *who, size_t who_size)
{
    SubAccount a;
    subGetAccount(&a);  // The key asked about.
    const JsonValue *body;
    char why[160];
    JsonDoc *info = subCall(NULL, "tokenInfo", NULL, &body, NULL, why, sizeof(why));
    bool known = info && subTokenUser(body, who, who_size);
    jsonFree(info);
    if (!known) {
        memset(&a, 0, sizeof(a));
        return false;
    }
    pthread_mutex_lock(&g_lock);
    if (subUsesApiKey(&g_account) && !strcmp(g_account.api_key, a.api_key))
        snprintf(g_account.user, sizeof(g_account.user), "%s", who);
    pthread_mutex_unlock(&g_lock);
    memset(&a, 0, sizeof(a));
    return true;
}

bool subTest(char *out, size_t size)
{
    const JsonValue *body;
    pthread_mutex_lock(&g_lock);
    bool with_key = subUsesApiKey(&g_account);
    pthread_mutex_unlock(&g_lock);
    int error;
    JsonDoc *ping = call(NULL, "ping", NULL, &body, NULL, out, size, &error);
    if (!ping && with_key && error && error != SUB_ERROR_BAD_KEY && offersApiKeys() == 0)
        snprintf(out, size, "%s", T("This server doesn't take API keys. Sign in with your user name and password."));
    if (!ping)
        return false;
    char name[64], version[64];
    snprintf(name, sizeof(name), "%s", jsonText(body, "type")[0] ? jsonText(body, "type") : "Subsonic");
    name[0] = (char)toupper((unsigned char)name[0]);  // "navidrome": "Navidrome".
    snprintf(version, sizeof(version), "%s", jsonText(body, "serverVersion"));
    version[strcspn(version, " ")] = '\0';  // "0.64.2 (10114574)": its number.
    jsonFree(ping);
    char why[160];
    JsonDoc *scan = subCall(NULL, "getScanStatus", NULL, &body, NULL, why, sizeof(why));
    long songs = scan ? subScanCount(body) : -1;
    jsonFree(scan);
    SubAccount a;
    subGetAccount(&a);
    const char *plain = davAddressIsExposed(a.address)
                            ? T(" The connection isn't encrypted: use https outside your home.")
                            : "";
    char who[SUB_TEXT_MAX];
    snprintf(who, sizeof(who), "%s", a.user);
    memset(&a, 0, sizeof(a));
    if (with_key && !learnKeyUser(who, sizeof(who)))
        snprintf(who, sizeof(who), "%s", T("the API key's user"));
    if (songs >= 0)
        snprintf(out, size, T("%s %s answers. Signed in as %s: %ld songs.%s"), name, version, who, songs, plain);
    else
        snprintf(out, size, T("%s %s answers. Signed in as %s.%s"), name, version, who, plain);
    return true;
}

// --- The library ---

typedef struct {
    NetClient *client;
    SubStop stop;
    SubProgress progress;
    void *user;
    size_t total;
} Fetch;

static bool stopped(const Fetch *f)
{
    return f->stop && f->stop();
}

static void report(const Fetch *f, const Catalog *c)
{
    if (f->progress)
        f->progress(c->song_count, f->total > c->song_count ? f->total : c->song_count, f->user);
}

static size_t countItems(const JsonValue *parent, const char *name)
{
    size_t n = 0;
    for (const JsonValue *v = subFirst(parent, name); v; v = subNext(parent, name, v))
        n++;
    return n;
}

// search3 with no words, page by page. *items: the songs the server gave, played or not.
static bool fetchSearch(Fetch *f, Catalog *c, size_t *items, char *why, size_t why_size)
{
    *items = 0;
    for (size_t offset = 0; offset < SUB_SONGS_MAX && !stopped(f); offset += PAGE_SONGS) {
        char params[128];
        snprintf(params, sizeof(params), "query=&artistCount=0&albumCount=0&songCount=%d&songOffset=%zu",
                 PAGE_SONGS, offset);
        const JsonValue *body;
        JsonDoc *doc = subCall(f->client, "search3", params, &body, NULL, why, why_size);
        if (!doc)
            return false;
        const JsonValue *result = jsonGet(body, "searchResult3");
        size_t n = countItems(result, "song"), added = 0;
        bool ok = subAddSongs(c, result, "song", NULL, &added);
        jsonFree(doc);
        if (!ok) {
            snprintf(why, why_size, "%s", T("There isn't enough memory for the whole library."));
            return false;
        }
        *items += n;
        report(f, c);
        if (n < PAGE_SONGS)
            return true;
    }
    return !stopped(f);
}

// One album's songs. One the server no longer has (gone since the list was made) is left out.
static bool fetchAlbum(Fetch *f, Catalog *c, const char *id, char *why, size_t why_size)
{
    char encoded[SUB_ID_MAX], params[SUB_ID_MAX + 8];
    if (!subEncode(id, encoded, sizeof(encoded)))
        return true;  // An id too long to ask for: left out.
    snprintf(params, sizeof(params), "id=%s", encoded);
    const JsonValue *body;
    int error;
    JsonDoc *doc = call(f->client, "getAlbum", params, &body, NULL, why, why_size, &error);
    if (!doc && error == SUB_ERROR_NOT_FOUND) {
        snprintf(why, why_size, "%s", "");
        return true;
    }
    if (!doc)
        return false;
    const JsonValue *album = jsonGet(body, "album");
    size_t added = 0;
    bool ok = subAddSongs(c, album, "song", jsonText(album, "artist"), &added);
    jsonFree(doc);
    if (!ok)
        snprintf(why, why_size, "%s", T("There isn't enough memory for the whole library."));
    return ok;
}

// getAlbumList2, then each album: for servers whose search3 finds nothing without words.
static bool fetchAlbums(Fetch *f, Catalog *c, char *why, size_t why_size)
{
    for (size_t offset = 0; offset < SUB_SONGS_MAX && !stopped(f); offset += PAGE_ALBUMS) {
        char params[96];
        snprintf(params, sizeof(params), "type=alphabeticalByName&size=%d&offset=%zu", PAGE_ALBUMS, offset);
        const JsonValue *body;
        JsonDoc *doc = subCall(f->client, "getAlbumList2", params, &body, NULL, why, why_size);
        if (!doc)
            return false;
        const JsonValue *list = jsonGet(body, "albumList2");
        size_t n = 0;
        bool ok = true;
        for (const JsonValue *a = subFirst(list, "album"); a && ok && !stopped(f); a = subNext(list, "album", a)) {
            n++;
            char id[SUB_ID_MAX];
            snprintf(id, sizeof(id), "%s", jsonText(a, "id"));
            ok = !id[0] || fetchAlbum(f, c, id, why, why_size);
            report(f, c);
        }
        jsonFree(doc);
        if (!ok)
            return false;
        if (n < PAGE_ALBUMS)
            break;
    }
    return !stopped(f);
}

bool subFetchLibrary(Catalog *c, SubStop stop, SubProgress progress, void *user, char *why, size_t why_size)
{
    snprintf(why, why_size, "%s", "");
    Fetch f = {netClientOpen(), stop, progress, user, 0};
    if (!f.client) {
        snprintf(why, why_size, "%s", T("The network can't be used."));
        return false;
    }
    const JsonValue *body;
    JsonDoc *scan = subCall(f.client, "getScanStatus", NULL, &body, NULL, why, why_size);
    long count = scan ? subScanCount(body) : -1;
    jsonFree(scan);
    f.total = count > 0 ? (size_t)count : 0;  // Without it (an old server), a spinner.
    snprintf(why, why_size, "%s", "");
    report(&f, c);
    size_t items = 0;
    bool ok = fetchSearch(&f, c, &items, why, why_size);
    if (ok && items == 0 && !stopped(&f))
        ok = fetchAlbums(&f, c, why, why_size);
    netClientClose(f.client);
    if (stopped(&f))
        snprintf(why, why_size, "%s", "");
    return ok && !stopped(&f);
}

// --- Plays, songs and covers ---

bool subScrobble(const char *location, bool submission)
{
    SubTarget t;
    if (!subParseLocation(location, &t) || t.cover)
        return false;
    char params[SUB_ID_MAX + 48], why[160];
    snprintf(params, sizeof(params), "id=%s&submission=%s", t.id, submission ? "true" : "false");
    const JsonValue *body;
    JsonDoc *doc = subCall(NULL, "scrobble", params, &body, NULL, why, sizeof(why));
    jsonFree(doc);
    return doc != NULL;
}

char *subFetchLyrics(const char *location, const volatile bool *cancel)
{
    SubTarget t;
    if (!subParseLocation(location, &t) || t.cover)
        return NULL;
    char params[SUB_ID_MAX + 8], why[160];
    snprintf(params, sizeof(params), "id=%s", t.id);
    const JsonValue *body;
    JsonDoc *doc = subCall(NULL, "getLyricsBySongId", params, &body, cancel, why, sizeof(why));
    char *text = doc ? malloc(LYRICS_FILE_MAX) : NULL;
    if (text && !subLyricsText(body, text, LYRICS_FILE_MAX)) {
        free(text);  // None, or a server without OpenSubsonic's lyrics.
        text = NULL;
    }
    jsonFree(doc);
    return text;
}

bool subSongUrl(const char *location, bool raw_stream, char *url, size_t size, bool *any_certificate)
{
    SubTarget t;
    SubAccount a;
    subGetAccount(&a);
    char params[SUB_ID_MAX + 24];
    bool ok = subParseLocation(location, &t) && !t.cover && subSignedIn(&a);
    if (ok) {
        snprintf(params, sizeof(params), "id=%s%s", t.id, raw_stream ? "&format=raw" : "");
        ok = subRequestUrl(&a, raw_stream ? "stream" : "download", params, url, size);
    }
    *any_certificate = !a.check_certificate;
    memset(&a, 0, sizeof(a));
    return ok;
}

bool subCoverUrl(const char *location, char *url, size_t size, bool *any_certificate)
{
    SubTarget t;
    SubAccount a;
    subGetAccount(&a);
    char params[SUB_ID_MAX + 24];
    bool ok = subParseLocation(location, &t) && t.cover && subSignedIn(&a);
    if (ok) {
        snprintf(params, sizeof(params), "id=%s&size=%d", t.id, t.px);
        ok = subRequestUrl(&a, "getCoverArt", params, url, size);
    }
    *any_certificate = !a.check_certificate;
    memset(&a, 0, sizeof(a));
    return ok;
}
