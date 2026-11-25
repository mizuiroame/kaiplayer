#include "subsonic.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

#define HTTP "http://"
#define HTTPS "https://"
#define SUFFIX_MAX 8
#define COVER_NAME "cover-"
#define COVER_EXTENSION ".jpg"
#define COVER_PX_MAX 4096

bool subIsLocation(const char *path)
{
    return path && !strncmp(path, SUB_PREFIX, sizeof(SUB_PREFIX) - 1);
}

// --- Addresses ---

static size_t schemeLength(const char *s)
{
    if (!strncasecmp(s, HTTP, sizeof(HTTP) - 1))
        return sizeof(HTTP) - 1;
    if (!strncasecmp(s, HTTPS, sizeof(HTTPS) - 1))
        return sizeof(HTTPS) - 1;
    return 0;
}

// Takes end off s's last length bytes if they are it, ignoring case.
static size_t without(const char *s, size_t length, const char *end)
{
    size_t n = strlen(end);
    return length >= n && !strncasecmp(s + length - n, end, n) ? length - n : length;
}

bool subNormalizeAddress(const char *typed, char *out, size_t size)
{
    while (*typed == ' ')
        typed++;
    size_t length = strlen(typed);
    while (length && typed[length - 1] == ' ')
        length--;
    if (!length || memchr(typed, ' ', length))
        return false;
    size_t scheme = schemeLength(typed);
    if (!scheme && strstr(typed, "://"))
        return false;  // ftp://, smb://...: not a server's.
    size_t cut = scheme + strcspn(typed + scheme, "?#");  // Without a page's query or anchor.
    length = cut < length ? cut : length;
    size_t authority = strcspn(typed + scheme, "/?#");
    authority = authority < length - scheme ? authority : length - scheme;
    if (!authority || memchr(typed + scheme, '@', authority))
        return false;  // Nothing after the scheme, or "user:password@host".
    // Navidrome's own page, or the API's folder, typed as they show in a browser.
    for (int pass = 0; pass < 2; pass++) {
        while (length > scheme + authority && typed[length - 1] == '/')
            length--;
        length = without(typed, length, "/app");
        length = without(typed, length, "/rest");
    }
    while (length > scheme + authority && typed[length - 1] == '/')
        length--;
    if (length < scheme + authority)
        length = scheme + authority;
    int n = snprintf(out, size, "%s%.*s", scheme ? "" : HTTP, (int)length, typed);
    return n > 0 && (size_t)n < size;
}

void subShownAddress(const char *address, char *out, size_t size)
{
    const char *s = !strncasecmp(address, HTTP, sizeof(HTTP) - 1) ? address + sizeof(HTTP) - 1 : address;
    snprintf(out, size, "%s", s);
}

static size_t serverLength(const char *address)
{
    size_t scheme = schemeLength(address);
    return scheme + strcspn(address + scheme, "/");
}

bool subSameServer(const char *a, const char *b)
{
    size_t n = serverLength(a);
    return n == serverLength(b) && !strncasecmp(a, b, n);
}

bool subPointedAddress(const char *address, const char *redirected, char *out, size_t size)
{
    size_t scheme = schemeLength(redirected);
    if (!scheme)
        return false;
    size_t length = scheme + strcspn(redirected + scheme, "?#");
    while (length > scheme && redirected[length - 1] == '/')
        length--;
    size_t page = without(redirected, length, "/app");
    if (page == length || page <= scheme + strcspn(redirected + scheme, "/?#"))
        return false;  // Not Navidrome's page, or it's at the server's top.
    char pointed[SUB_ADDRESS_MAX];
    if (!subNormalizeAddress(redirected, pointed, sizeof(pointed)) || !strcasecmp(pointed, address))
        return false;
    int n = snprintf(out, size, "%s", pointed);
    return n > 0 && (size_t)n < size;
}

// --- The sign-in and the account's file ---

bool subSetPassword(SubAccount *a, const char *password, const char *salt)
{
    size_t length = strlen(salt);
    if (!length || length >= sizeof(a->salt))
        return false;
    for (size_t i = 0; i < length; i++)
        if (!isalnum((unsigned char)salt[i]))
            return false;
    char joined[SUB_TEXT_MAX + SUB_SALT_MAX];
    int n = snprintf(joined, sizeof(joined), "%s%s", password, salt);
    bool ok = n > 0 && (size_t)n < sizeof(joined);
    if (ok) {
        md5Hex(joined, (size_t)n, a->token);
        snprintf(a->salt, sizeof(a->salt), "%s", salt);
    }
    memset(joined, 0, sizeof(joined));
    return ok;
}

bool subSignedIn(const SubAccount *a)
{
    return a->address[0] && a->user[0] && a->salt[0] && strlen(a->token) == MD5_HEX_SIZE - 1;
}

static void readValue(char *out, size_t size, const char *value, size_t length)
{
    snprintf(out, size, "%.*s", (int)(length < size ? length : size - 1), value);
}

static bool keyIs(const char *line, size_t key, const char *name)
{
    return key == strlen(name) && !strncmp(line, name, key);
}

void subAccountRead(SubAccount *a, const char *text)
{
    memset(a, 0, sizeof(*a));
    a->check_certificate = true;
    for (const char *line = text; *line;) {
        size_t length = strcspn(line, "\r\n");
        const char *eq = memchr(line, '=', length);
        if (eq) {
            size_t key = (size_t)(eq - line), value = length - key - 1;
            if (keyIs(line, key, "address"))
                readValue(a->address, sizeof(a->address), eq + 1, value);
            else if (keyIs(line, key, "user"))
                readValue(a->user, sizeof(a->user), eq + 1, value);
            else if (keyIs(line, key, "salt"))
                readValue(a->salt, sizeof(a->salt), eq + 1, value);
            else if (keyIs(line, key, "token"))
                readValue(a->token, sizeof(a->token), eq + 1, value);
            else if (keyIs(line, key, "check_certificate"))
                a->check_certificate = !(value == 1 && eq[1] == '0');
        }
        line += length;
        line += strspn(line, "\r\n");
    }
    char address[SUB_ADDRESS_MAX];
    if (a->address[0] && subNormalizeAddress(a->address, address, sizeof(address)))
        memcpy(a->address, address, sizeof(address));
    else
        a->address[0] = '\0';
    for (const char *p = a->salt; *p; p++)
        if (!isalnum((unsigned char)*p))
            a->salt[0] = a->token[0] = '\0';  // Damaged: signed out.
    for (const char *p = a->token; *p; p++)
        if (!isxdigit((unsigned char)*p))
            a->salt[0] = a->token[0] = '\0';
}

bool subAccountWrite(const SubAccount *a, char *out, size_t size)
{
    if (strpbrk(a->address, "\r\n") || strpbrk(a->user, "\r\n") || strpbrk(a->salt, "\r\n") ||
        strpbrk(a->token, "\r\n"))
        return false;
    int n = snprintf(out, size, "address=%s\nuser=%s\nsalt=%s\ntoken=%s\ncheck_certificate=%d\n", a->address,
                     a->user, a->salt, a->token, a->check_certificate ? 1 : 0);
    return n > 0 && (size_t)n < size;
}

// --- URLs ---

static bool unreserved(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

bool subEncode(const char *text, char *out, size_t size)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (n + 4 > size)
            return false;
        if (unreserved(*p))
            out[n++] = (char)*p;
        else
            n += (size_t)snprintf(out + n, 4, "%%%02X", *p);
    }
    if (n >= size)
        return false;
    out[n] = '\0';
    return true;
}

bool subRequestUrl(const SubAccount *a, const char *method, const char *params, char *out, size_t size)
{
    char user[SUB_TEXT_MAX * 3];
    if (!a->address[0] || !subEncode(a->user, user, sizeof(user)))
        return false;
    int n = snprintf(out, size, "%s/rest/%s?u=%s&t=%s&s=%s&v=" SUB_API_VERSION "&c=" SUB_CLIENT "&f=json%s%s",
                     a->address, method, user, a->token, a->salt, params && params[0] ? "&" : "",
                     params ? params : "");
    return n > 0 && (size_t)n < size;
}

// --- Locations ---

bool subSongLocation(const char *id, const char *cover, const char *suffix, char *out, size_t size)
{
    char id_part[SUB_ID_MAX], cover_part[SUB_ID_MAX], extension[SUFFIX_MAX + 1];
    size_t length = strlen(suffix);
    if (!id[0] || !length || length > SUFFIX_MAX)
        return false;
    for (size_t i = 0; i <= length; i++) {
        if (suffix[i] && !isalnum((unsigned char)suffix[i]))
            return false;
        extension[i] = (char)tolower((unsigned char)suffix[i]);
    }
    if (!subEncode(id, id_part, sizeof(id_part)) || !subEncode(cover[0] ? cover : SUB_NO_COVER, cover_part,
                                                               sizeof(cover_part)))
        return false;
    int n = snprintf(out, size, SUB_PREFIX "%s/%s.%s", cover_part, id_part, extension);
    return n > 0 && (size_t)n < size;
}

// A location's cover part (after the prefix, before the '/') and its name. False if it isn't one.
static bool splitLocation(const char *location, char *cover, size_t cover_size, const char **name)
{
    if (!subIsLocation(location))
        return false;
    const char *rest = location + sizeof(SUB_PREFIX) - 1;
    const char *slash = strchr(rest, '/');
    if (!slash || slash == rest || (size_t)(slash - rest) >= cover_size || strchr(slash + 1, '/'))
        return false;
    snprintf(cover, cover_size, "%.*s", (int)(slash - rest), rest);
    *name = slash + 1;
    return true;
}

bool subCoverLocation(const char *song, int px, char *out, size_t size)
{
    char cover[SUB_ID_MAX];
    const char *name;
    if (!splitLocation(song, cover, sizeof(cover), &name) || !strcmp(cover, SUB_NO_COVER) || px <= 0 ||
        px > COVER_PX_MAX)
        return false;
    int n = snprintf(out, size, SUB_PREFIX "%s/" COVER_NAME "%d" COVER_EXTENSION, cover, px);
    return n > 0 && (size_t)n < size;
}

// An id as subEncode leaves it: unreserved characters and "%XX". Anything else ('&', '=', '#')
// would add to the query it goes into.
static bool idIsPlain(const char *id)
{
    for (const char *p = id; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
            continue;
        if (c == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            p += 2;
            continue;
        }
        return false;
    }
    return id[0] != '\0';
}

bool subParseLocation(const char *location, SubTarget *out)
{
    memset(out, 0, sizeof(*out));
    char cover[SUB_ID_MAX];
    const char *name;
    if (!splitLocation(location, cover, sizeof(cover), &name))
        return false;
    size_t length = strlen(name), prefix = sizeof(COVER_NAME) - 1, extension = sizeof(COVER_EXTENSION) - 1;
    if (length > prefix + extension && !strncmp(name, COVER_NAME, prefix) &&
        !strcmp(name + length - extension, COVER_EXTENSION)) {
        char *end;
        long px = strtol(name + prefix, &end, 10);
        if (end != name + length - extension || px <= 0 || px > COVER_PX_MAX || !strcmp(cover, SUB_NO_COVER))
            return false;
        out->cover = true;
        out->px = (int)px;
        snprintf(out->id, sizeof(out->id), "%s", cover);
        return idIsPlain(out->id);
    }
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name || (size_t)(dot - name) >= sizeof(out->id))
        return false;
    snprintf(out->id, sizeof(out->id), "%.*s", (int)(dot - name), name);
    return idIsPlain(out->id);
}

// --- Answers ---

bool subAnswer(const JsonValue *root, const JsonValue **body, int *code, char *message, size_t message_size)
{
    const JsonValue *r = jsonGet(root, "subsonic-response");
    *body = r;
    *code = 0;
    snprintf(message, message_size, "%s", "");
    if (!r || r->type != JSON_OBJECT)
        return false;
    if (!strcmp(jsonText(r, "status"), "ok"))
        return true;
    const JsonValue *error = jsonGet(r, "error");
    *code = (int)jsonInt(error, "code");
    snprintf(message, message_size, "%s", jsonText(error, "message"));
    return false;
}

const JsonValue *subFirst(const JsonValue *parent, const char *name)
{
    const JsonValue *m = jsonGet(parent, name);
    if (m && m->type == JSON_ARRAY)
        return m->child;
    return m && m->type == JSON_OBJECT ? m : NULL;
}

const JsonValue *subNext(const JsonValue *parent, const char *name, const JsonValue *item)
{
    const JsonValue *m = jsonGet(parent, name);
    return m && m->type == JSON_ARRAY && item ? item->next : NULL;
}

static bool isTrue(const JsonValue *v, const char *key)
{
    const JsonValue *m = jsonGet(v, key);
    return m && (m->type == JSON_TRUE || (m->type == JSON_STRING && !strcmp(m->text, "true")));
}

// An id, which a few servers give as a number.
static void idOf(const JsonValue *v, const char *key, char *out, size_t size)
{
    const JsonValue *m = jsonGet(v, key);
    if (m && m->type == JSON_NUMBER)
        snprintf(out, size, "%lld", jsonInt(v, key));
    else
        snprintf(out, size, "%s", m && m->type == JSON_STRING ? m->text : "");
}

// The file's extension: the song's "suffix", or its path's.
static const char *suffixOf(const JsonValue *song)
{
    const char *suffix = jsonText(song, "suffix");
    if (suffix[0])
        return suffix;
    const char *path = jsonText(song, "path"), *dot = strrchr(path, '.');
    return dot && !strchr(dot, '/') ? dot + 1 : "";
}

// OpenSubsonic's album artist (Navidrome's), else the one its album gave, else none.
static const char *albumArtistOf(const JsonValue *song, const char *fallback)
{
    const char *name = jsonText(song, "displayAlbumArtist");
    if (name[0])
        return name;
    const JsonValue *first = subFirst(song, "albumArtists");
    if (first && jsonText(first, "name")[0])
        return jsonText(first, "name");
    name = jsonText(song, "albumArtist");
    return name[0] ? name : fallback ? fallback : "";
}

static int number(const JsonValue *v, const char *key)
{
    long long n = jsonInt(v, key);
    return n > 0 && n < 100000 ? (int)n : 0;
}

static void fillTags(const JsonValue *song, const char *album_artist, TrackTags *t)
{
    memset(t, 0, sizeof(*t));
    const char *title = jsonText(song, "title");
    if (!title[0]) {  // The file's name, without its folders and extension.
        const char *path = jsonText(song, "path"), *slash = strrchr(path, '/');
        title = slash ? slash + 1 : path;
    }
    snprintf(t->title, sizeof(t->title), "%s", title);
    if (!jsonText(song, "title")[0] && strrchr(t->title, '.'))
        *strrchr(t->title, '.') = '\0';
    snprintf(t->artist, sizeof(t->artist), "%s", jsonText(song, "artist"));
    const char *album = jsonText(song, "album");
    snprintf(t->album, sizeof(t->album), "%s", album[0] ? album : "Unknown album");
    snprintf(t->album_artist, sizeof(t->album_artist), "%s", albumArtistOf(song, album_artist));
    t->track_number = number(song, "track");
    t->disc_number = number(song, "discNumber");
    t->year = number(song, "year");
}

bool subAddSongs(Catalog *c, const JsonValue *parent, const char *name, const char *album_artist, size_t *added)
{
    TrackTags *tags = malloc(sizeof(TrackTags));
    if (!tags)
        return false;
    bool ok = true;
    for (const JsonValue *s = subFirst(parent, name); s && ok && c->song_count < SUB_SONGS_MAX;
         s = subNext(parent, name, s)) {
        char id[SUB_ID_MAX], cover[SUB_ID_MAX], location[UTIL_PATH_MAX];
        idOf(s, "id", id, sizeof(id));
        idOf(s, "coverArt", cover, sizeof(cover));
        if (s->type != JSON_OBJECT || isTrue(s, "isDir") || isTrue(s, "isVideo") ||
            !subSongLocation(id, cover, suffixOf(s), location, sizeof(location)) || !utilIsAudioFile(location))
            continue;  // Not a song KaiPlayer can play.
        fillTags(s, album_artist, tags);
        long long size = jsonInt(s, "size");
        ok = catalogAddFile(c, location, size > 0 && size < 0x7FFFFFFF ? (long)size : 0, 0, tags, SUB_ROOT);
        if (ok)
            (*added)++;
    }
    free(tags);
    return ok;
}

long subScanCount(const JsonValue *body)
{
    const JsonValue *status = jsonGet(body, "scanStatus");
    return jsonHasNumber(status, "count") ? (long)jsonInt(status, "count") : -1;
}

// Appends text to out at *used; false when it doesn't fit.
static bool appendText(char *out, size_t size, size_t *used, const char *text)
{
    size_t n = strlen(text);
    if (*used + n + 1 > size)
        return false;
    memcpy(out + *used, text, n + 1);
    *used += n;
    return true;
}

// One structuredLyrics' lines into out: "[mm:ss.xx]words" when synced, else the words.
static bool lyricsLines(const JsonValue *lyrics, bool synced, char *out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    long long offset = jsonInt(lyrics, "offset");
    bool any = false;
    for (const JsonValue *line = subFirst(lyrics, "line"); line; line = subNext(lyrics, "line", line)) {
        char stamp[24] = "";
        if (synced) {
            long long ms = jsonInt(line, "start") + offset;
            if (ms < 0)
                ms = 0;
            snprintf(stamp, sizeof(stamp), "[%02lld:%02lld.%02lld]", ms / 60000, ms / 1000 % 60, ms / 10 % 100);
        }
        const char *words = jsonText(line, "value");
        if (!appendText(out, size, &used, stamp) || !appendText(out, size, &used, words) ||
            !appendText(out, size, &used, "\n"))
            return false;
        any = any || words[0];
    }
    return any;
}

bool subLyricsText(const JsonValue *body, char *out, size_t size)
{
    const JsonValue *list = jsonGet(body, "lyricsList");
    if (!list || size == 0)
        return false;
    for (int pass = 0; pass < 2; pass++) {  // Synced lyrics first, then plain ones.
        bool want_synced = pass == 0;
        for (const JsonValue *l = subFirst(list, "structuredLyrics"); l; l = subNext(list, "structuredLyrics", l)) {
            const JsonValue *synced = jsonGet(l, "synced");
            if ((synced && synced->type == JSON_TRUE) == want_synced && lyricsLines(l, want_synced, out, size))
                return true;
        }
    }
    out[0] = '\0';
    return false;
}
