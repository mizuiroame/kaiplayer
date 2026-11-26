#include "stream.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lang.h"
#include "stream_text.h"

#define JAMENDO_API "https://api.jamendo.com/v3.0/"
#define JAMENDO_COVERS "https://usercontent.jamendo.com"
#define ARCHIVE_SEARCH "https://archive.org/advancedsearch.php"
#define ARCHIVE_METADATA "https://archive.org/metadata/"
#define ARCHIVE_DOWNLOAD "https://archive.org/download/"
#define ARCHIVE_IMAGE "https://archive.org/services/img/"
#define CC_HOST "creativecommons.org/"
#define NO_LICENSE "-"
#define NO_COVER "-"
#define LMA_LICENSE "lma"
#define COVER_FOLDER "cover"
#define ID_DIGITS_MAX 20
#define IDENTIFIER_MAX 100
#define DOT "\xC2\xB7"  // The middle dot between a credit's parts.

// What the Archive is searched in: only what has a clear permission (STREAMING-STATUS.md).
#define Q_LIVE "collection:etree AND mediatype:etree"
#define Q_NETLABELS "collection:netlabels AND mediatype:audio AND licenseurl:*creativecommons*"
#define Q_FREE "mediatype:audio AND licenseurl:*creativecommons* AND NOT collection:librivoxaudio"
#define ARCHIVE_FIELDS "&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=creator&fl%5B%5D=date&fl%5B%5D=licenseurl"
#define BY_DOWNLOADS "&sort%5B%5D=downloads+desc"

bool streamIsLocation(const char *path)
{
    return path && (!strncmp(path, STREAM_JAMENDO_PREFIX, sizeof(STREAM_JAMENDO_PREFIX) - 1) ||
                    !strncmp(path, STREAM_ARCHIVE_PREFIX, sizeof(STREAM_ARCHIVE_PREFIX) - 1));
}

// --- Small helpers ---

// text into out, cut at a whole UTF-8 character if it doesn't fit, control characters as spaces.
void streamCopyText(char *out, size_t size, const char *text)
{
    if (!size)
        return;
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p && n + 1 < size; p++)
        out[n++] = *p < 0x20 || *p == 0x7F ? ' ' : (char)*p;
    out[n] = '\0';
    if (text && strlen(text) > n) {  // Cut: drop a character left half.
        size_t start = n;
        while (start > 0 && ((unsigned char)out[start - 1] & 0xC0) == 0x80)
            start--;
        if (start > 0 && ((unsigned char)out[start - 1] & 0x80)) {
            unsigned char lead = (unsigned char)out[start - 1];
            size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
            if (n - (start - 1) < need)
                out[start - 1] = '\0';
        }
    }
}

bool streamAllDigits(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (!n || n > max)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i]))
            return false;
    return true;
}

// An Archive identifier: letters, digits, '.', '-', '_'; not "." or "..".
bool streamIdentifierValid(const char *s)
{
    size_t n = strlen(s);
    if (!n || n > IDENTIFIER_MAX || !strcmp(s, ".") || !strcmp(s, ".."))
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i]) && s[i] != '.' && s[i] != '-' && s[i] != '_')
            return false;
    return true;
}

// A license code: lowercase letters, digits, '.', '-'.
static bool licenseValid(const char *s)
{
    size_t n = strlen(s);
    if (!n || n >= STREAM_LICENSE_MAX)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!islower((unsigned char)s[i]) && !isdigit((unsigned char)s[i]) && s[i] != '.' && s[i] != '-')
            return false;
    return true;
}

// A Jamendo cover's key: "-", an album's id, or "a" and an artist's.
static bool coverKeyValid(const char *s)
{
    return !strcmp(s, NO_COVER) || streamAllDigits(s, ID_DIGITS_MAX) || (s[0] == 'a' && streamAllDigits(s + 1, ID_DIGITS_MAX));
}

static bool unreserved(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

// Percent-encodes text (keep_slash: '/' as it is). False if it doesn't fit.
static bool encode(const char *text, bool keep_slash, char *out, size_t size)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (n + 4 > size)
            return false;
        if (unreserved(*p) || (keep_slash && *p == '/'))
            out[n++] = (char)*p;
        else
            n += (size_t)snprintf(out + n, 4, "%%%02X", *p);
    }
    if (n >= size)
        return false;
    out[n] = '\0';
    return true;
}

static int hexValue(char c)
{
    return isdigit((unsigned char)c) ? c - '0' : isxdigit((unsigned char)c) ? tolower((unsigned char)c) - 'a' + 10 : -1;
}

// Decodes %XX. False for a broken escape, a NUL or control character, or if it doesn't fit.
static bool decode(const char *text, size_t length, char *out, size_t size)
{
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        int c = (unsigned char)text[i];
        if (c == '%') {
            if (i + 2 >= length)
                return false;
            int hi = hexValue(text[i + 1]), lo = hexValue(text[i + 2]);
            if (hi < 0 || lo < 0)
                return false;
            c = hi * 16 + lo;
            i += 2;
        }
        if (c < 0x20 || c == 0x7F || n + 1 >= size)
            return false;
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return n > 0;
}

// The extension of a file KaiPlayer plays from these services, in lowercase ("mp3"), or NULL.
const char *streamPlayableExtension(const char *file)
{
    static const char *const KNOWN[] = {"mp3", "ogg", "flac"};
    const char *dot = strrchr(file, '.');
    if (!dot || strchr(dot, '/'))
        return NULL;
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++)
        if (!strcasecmp(dot + 1, KNOWN[i]))
            return KNOWN[i];
    return NULL;
}

// --- Locations ---

bool streamJamendoLocation(const char *album_id, const char *license_url, const char *track_id, char *out,
                           size_t size)
{
    const char *cover = album_id && album_id[0] && strcmp(album_id, "0") ? album_id : NO_COVER;
    if (!streamAllDigits(track_id, ID_DIGITS_MAX) || !coverKeyValid(cover) || cover[0] == 'a')
        return false;
    char license[STREAM_LICENSE_MAX];
    streamLicenseCode(license_url, license, sizeof(license));
    int n = snprintf(out, size, STREAM_JAMENDO_PREFIX "%s/%s/%s.mp3", cover, license, track_id);
    return n > 0 && (size_t)n < size;
}

bool streamArchiveLocation(const char *identifier, const char *license, const char *file, char *out, size_t size)
{
    char encoded[UTIL_PATH_MAX];
    if (!streamIdentifierValid(identifier) || !licenseValid(license) || !streamPlayableExtension(file) ||
        strlen(file) >= STREAM_FILE_MAX || !encode(file, false, encoded, sizeof(encoded)))
        return false;
    for (const char *p = file; *p; p++)
        if ((unsigned char)*p < 0x20)
            return false;
    int n = snprintf(out, size, STREAM_ARCHIVE_PREFIX "%s/%s/%s", identifier, license, encoded);
    return n > 0 && (size_t)n < size;
}

bool streamCoverKeyLocation(StreamService service, const char *key, char *out, size_t size)
{
    bool ok = service == STREAM_JAMENDO ? coverKeyValid(key) && strcmp(key, NO_COVER) : streamIdentifierValid(key);
    int n = ok ? snprintf(out, size, "%s%s/" COVER_FOLDER "/0.jpg",
                          service == STREAM_JAMENDO ? STREAM_JAMENDO_PREFIX : STREAM_ARCHIVE_PREFIX, key)
               : -1;
    return n > 0 && (size_t)n < size;
}

bool streamCoverLocation(const char *location, int px, char *out, size_t size)
{
    StreamTarget t;
    if (!streamParseLocation(location, &t) || px <= 0 || px > 4096)
        return false;
    if (t.service == STREAM_JAMENDO && !strcmp(t.key, NO_COVER))
        return false;  // A single: no album cover to ask for.
    int n = snprintf(out, size, "%s%s/" COVER_FOLDER "/%d.jpg",
                     t.service == STREAM_JAMENDO ? STREAM_JAMENDO_PREFIX : STREAM_ARCHIVE_PREFIX, t.key, px);
    return n > 0 && (size_t)n < size;
}

// "a/b/c" after the prefix: its three parts (the third is the rest, slashes and all).
static bool splitLocation(const char *rest, char *first, size_t first_size, char *second, size_t second_size,
                          const char **third)
{
    const char *a = strchr(rest, '/');
    if (!a || (size_t)(a - rest) >= first_size || a == rest)
        return false;
    const char *b = strchr(a + 1, '/');
    if (!b || (size_t)(b - a - 1) >= second_size || b == a + 1 || !b[1])
        return false;
    memcpy(first, rest, (size_t)(a - rest));
    first[a - rest] = '\0';
    memcpy(second, a + 1, (size_t)(b - a - 1));
    second[b - a - 1] = '\0';
    *third = b + 1;
    return true;
}

// "300.jpg" -> 300 (1 to 4096, or 0: "0.jpg", a key's location).
static bool coverPx(const char *name, int *px)
{
    char digits[8];
    const char *dot = strchr(name, '.');
    size_t n = dot ? (size_t)(dot - name) : 0;
    if (!n || n >= sizeof(digits) || strcmp(dot, ".jpg"))
        return false;
    memcpy(digits, name, n);
    digits[n] = '\0';
    if (!streamAllDigits(digits, 4))
        return false;
    *px = atoi(digits);
    return *px <= 4096;
}

bool streamParseLocation(const char *location, StreamTarget *out)
{
    memset(out, 0, sizeof(*out));
    if (!streamIsLocation(location) || strlen(location) >= UTIL_PATH_MAX)
        return false;
    bool jamendo = location[0] == 'j';
    out->service = jamendo ? STREAM_JAMENDO : STREAM_ARCHIVE;
    const char *rest = location + (jamendo ? sizeof(STREAM_JAMENDO_PREFIX) : sizeof(STREAM_ARCHIVE_PREFIX)) - 1;
    char second[STREAM_LICENSE_MAX];
    const char *third;
    if (!splitLocation(rest, out->key, sizeof(out->key), second, sizeof(second), &third))
        return false;
    if (jamendo ? !coverKeyValid(out->key) : !streamIdentifierValid(out->key))
        return false;
    if (!strcmp(second, COVER_FOLDER)) {
        out->cover = true;
        return (!jamendo || strcmp(out->key, NO_COVER)) && coverPx(third, &out->px);
    }
    if (!licenseValid(second) || (jamendo && out->key[0] == 'a'))
        return false;
    snprintf(out->license, sizeof(out->license), "%s", second);
    if (jamendo) {
        size_t n = strlen(third);
        if (n < 5 || strcmp(third + n - 4, ".mp3") || n - 4 >= sizeof(out->id))
            return false;
        memcpy(out->id, third, n - 4);
        out->id[n - 4] = '\0';
        return streamAllDigits(out->id, ID_DIGITS_MAX);
    }
    return !strchr(third, '/') && decode(third, strlen(third), out->file, sizeof(out->file)) &&
           streamPlayableExtension(out->file);
}

// --- Licenses and credits ---

// The part of s after "creativecommons.org/", or NULL.
static const char *ccPath(const char *s)
{
    const char *host = s ? strstr(s, CC_HOST) : NULL;
    return host ? host + sizeof(CC_HOST) - 1 : NULL;
}

// The next path segment of *p (letters, digits, '.', '-'), lowercased, into out; *p moves past it.
static bool segment(const char **p, char *out, size_t size)
{
    size_t n = 0;
    while (**p == '/')
        (*p)++;
    while (**p && **p != '/' && **p != '?' && **p != '#') {
        char c = (char)tolower((unsigned char)**p);
        if (!islower((unsigned char)c) && !isdigit((unsigned char)c) && c != '.' && c != '-')
            return false;
        if (n + 1 >= size)
            return false;
        out[n++] = c;
        (*p)++;
    }
    out[n] = '\0';
    return n > 0;
}

void streamLicenseCode(const char *license_url, char *out, size_t size)
{
    snprintf(out, size, "%s", NO_LICENSE);
    const char *p = ccPath(license_url);
    char kind[16], name[16], version[8] = "";
    if (!p || !segment(&p, kind, sizeof(kind)) || !segment(&p, name, sizeof(name)))
        return;
    bool has_version = segment(&p, version, sizeof(version));
    if (has_version && (!isdigit((unsigned char)version[0]) || strspn(version, "0123456789.") != strlen(version)))
        has_version = false;
    char code[STREAM_LICENSE_MAX];
    if (!strcmp(kind, "licenses") && !strcmp(name, "publicdomain"))
        snprintf(code, sizeof(code), "pd");
    else if (!strcmp(kind, "publicdomain") && !strcmp(name, "zero"))
        snprintf(code, sizeof(code), "cc0%s%s", has_version ? "-" : "", has_version ? version : "");
    else if (!strcmp(kind, "publicdomain") && !strcmp(name, "mark"))
        snprintf(code, sizeof(code), "pdm%s%s", has_version ? "-" : "", has_version ? version : "");
    else if (!strcmp(kind, "licenses") && !strncmp(name, "by", 2) && strspn(name, "abcdefghijklmnopqrstuvwxyz-") == strlen(name))
        snprintf(code, sizeof(code), "%s%s%s", name, has_version ? "-" : "", has_version ? version : "");
    else
        return;
    if (licenseValid(code))
        snprintf(out, size, "%s", code);
}

// "by-nc-sa-3.0" -> name "by-nc-sa", version "3.0" ("" if none).
static void splitCode(const char *code, char *name, size_t name_size, char *version, size_t version_size)
{
    snprintf(name, name_size, "%s", code);
    version[0] = '\0';
    char *dash = strrchr(name, '-');
    if (dash && isdigit((unsigned char)dash[1])) {
        snprintf(version, version_size, "%s", dash + 1);
        *dash = '\0';
    }
}

void streamLicenseName(const char *code, char *out, size_t size)
{
    char name[STREAM_LICENSE_MAX], version[STREAM_LICENSE_MAX];
    out[0] = '\0';
    if (!code || !licenseValid(code) || !strcmp(code, NO_LICENSE))
        return;
    if (!strcmp(code, LMA_LICENSE)) {
        snprintf(out, size, "%s", T("Live Music Archive: non-commercial"));
        return;
    }
    splitCode(code, name, sizeof(name), version, sizeof(version));
    if (!strcmp(name, "pd") || !strcmp(name, "pdm")) {
        snprintf(out, size, "%s", T("Public domain"));
        return;
    }
    if (!strcmp(name, "cc0")) {
        snprintf(out, size, "CC0%s%s", version[0] ? " " : "", version);
        return;
    }
    for (char *c = name; *c; c++)
        *c = (char)toupper((unsigned char)*c);
    snprintf(out, size, "CC %s%s%s", name, version[0] ? " " : "", version);
}

void streamLicenseLink(const char *code, char *out, size_t size)
{
    char name[STREAM_LICENSE_MAX], version[STREAM_LICENSE_MAX];
    out[0] = '\0';
    if (!code || !licenseValid(code) || !strcmp(code, NO_LICENSE))
        return;
    if (!strcmp(code, LMA_LICENSE)) {
        snprintf(out, size, "archive.org/details/etree");
        return;
    }
    splitCode(code, name, sizeof(name), version, sizeof(version));
    const char *slash = version[0] ? "/" : "";
    if (!strcmp(name, "pd"))
        snprintf(out, size, CC_HOST "licenses/publicdomain");
    else if (!strcmp(name, "cc0"))
        snprintf(out, size, CC_HOST "publicdomain/zero%s%s", slash, version);
    else if (!strcmp(name, "pdm"))
        snprintf(out, size, CC_HOST "publicdomain/mark%s%s", slash, version);
    else
        snprintf(out, size, CC_HOST "licenses/%s%s%s", name, slash, version);
}

const char *streamServiceName(StreamService service)
{
    return service == STREAM_JAMENDO ? "Jamendo" : "Internet Archive";
}

bool streamPageLink(const char *location, char *out, size_t size)
{
    StreamTarget t;
    if (!streamParseLocation(location, &t) || t.cover)
        return false;
    int n = t.service == STREAM_JAMENDO ? snprintf(out, size, "jamendo.com/track/%s", t.id)
                                        : snprintf(out, size, "archive.org/details/%s", t.key);
    return n > 0 && (size_t)n < size;
}

bool streamCreditLine(const char *location, char *out, size_t size)
{
    StreamTarget t;
    char license[96], page[160];
    if (!streamParseLocation(location, &t) || t.cover || !streamPageLink(location, page, sizeof(page)))
        return false;
    streamLicenseName(t.license, license, sizeof(license));
    snprintf(out, size, "%s%s%s  " DOT "  %s", license, license[0] ? "  " DOT "  " : "",
             streamServiceName(t.service), page);
    return true;
}

// --- Requests ---

StreamService streamQueryService(StreamQueryKind kind)
{
    return kind >= STREAM_Q_IA_POPULAR_LIVE ? STREAM_ARCHIVE : STREAM_JAMENDO;
}

bool streamClientIdValid(const char *client_id)
{
    size_t n = client_id ? strlen(client_id) : 0;
    if (!n || n >= STREAM_CLIENT_ID_MAX)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)client_id[i]))
            return false;
    return true;
}

// Typed words without the spaces around them; false if none.
static bool trimmed(const char *typed, char *out, size_t size)
{
    while (*typed == ' ')
        typed++;
    streamCopyText(out, size, typed);
    size_t n = strlen(out);
    while (n && out[n - 1] == ' ')
        out[--n] = '\0';
    return n > 0;
}

bool streamArchiveWords(const char *typed, char *out, size_t size)
{
    if (!size)
        return false;
    size_t n = 0;
    bool space = true;  // No space first.
    for (const unsigned char *p = (const unsigned char *)typed; *p && n + 1 < size; p++) {
        bool keep = isalnum(*p) || *p >= 0x80;
        if (keep) {
            out[n++] = (char)tolower(*p);
            space = false;
        } else if (!space) {
            out[n++] = ' ';
            space = true;
        }
    }
    while (n && out[n - 1] == ' ')
        n--;
    while (n && ((unsigned char)out[n - 1] & 0xC0) == 0x80 && n + 1 >= size)
        n--;  // Cut in the middle of a character (at the end of the room).
    out[n] = '\0';
    return n > 0;
}

static bool jamendoUrl(const char *method, const char *client_id, const char *params, char *out, size_t size)
{
    if (!streamClientIdValid(client_id))
        return false;
    int n = snprintf(out, size, JAMENDO_API "%s/?client_id=%s&format=json%s", method, client_id, params);
    return n > 0 && (size_t)n < size;
}

static bool archiveSearchUrl(const char *query, int rows, bool by_downloads, char *out, size_t size)
{
    char q[STREAM_URL_MAX / 2];
    if (!encode(query, false, q, sizeof(q)))
        return false;
    int n = snprintf(out, size, ARCHIVE_SEARCH "?q=%s%s%s&rows=%d&page=1&output=json", q, ARCHIVE_FIELDS,
                     by_downloads ? BY_DOWNLOADS : "", rows);
    return n > 0 && (size_t)n < size;
}

static bool jamendoQueryUrl(const StreamQuery *q, const char *client_id, char *out, size_t size)
{
    char words[STREAM_WORDS_MAX], encoded[STREAM_WORDS_MAX * 3 + 1], params[STREAM_WORDS_MAX * 3 + 160];
    int limit = STREAM_PAGE_SIZE;
    const char *method = "tracks";
    bool searching = q->kind == STREAM_Q_JAM_SEARCH_TRACKS || q->kind == STREAM_Q_JAM_SEARCH_ALBUMS ||
                     q->kind == STREAM_Q_JAM_SEARCH_ARTISTS || q->kind == STREAM_Q_JAM_GENRE;
    if (searching && (!trimmed(q->words, words, sizeof(words)) || !encode(words, false, encoded, sizeof(encoded))))
        return false;
    bool by_id = q->kind == STREAM_Q_JAM_ARTIST_ALBUMS || q->kind == STREAM_Q_JAM_ALBUM_TRACKS ||
                 q->kind == STREAM_Q_JAM_AUDIO;
    if (by_id && !streamAllDigits(q->id, ID_DIGITS_MAX))
        return false;
    switch (q->kind) {
    case STREAM_Q_JAM_POPULAR:
        snprintf(params, sizeof(params), "&limit=%d&order=popularity_month&audioformat=mp32", limit);
        break;
    case STREAM_Q_JAM_NEW_ALBUMS:
        method = "albums";
        snprintf(params, sizeof(params), "&limit=%d&order=releasedate_desc", limit);
        break;
    case STREAM_Q_JAM_GENRE:
        snprintf(params, sizeof(params), "&limit=%d&tags=%s&order=popularity_month&audioformat=mp32", limit, encoded);
        break;
    case STREAM_Q_JAM_SEARCH_TRACKS:
        snprintf(params, sizeof(params), "&limit=%d&search=%s&boost=popularity_total&audioformat=mp32", limit, encoded);
        break;
    case STREAM_Q_JAM_SEARCH_ALBUMS:
        method = "albums";
        snprintf(params, sizeof(params), "&limit=%d&namesearch=%s&order=popularity_total", limit, encoded);
        break;
    case STREAM_Q_JAM_SEARCH_ARTISTS:
        method = "artists";
        snprintf(params, sizeof(params), "&limit=%d&namesearch=%s&order=popularity_total", limit, encoded);
        break;
    case STREAM_Q_JAM_ARTIST_ALBUMS:
        method = "albums";
        snprintf(params, sizeof(params), "&limit=%d&artist_id=%s&order=releasedate_desc", limit, q->id);
        break;
    case STREAM_Q_JAM_ALBUM_TRACKS:
        snprintf(params, sizeof(params), "&limit=200&album_id=%s&order=position&audioformat=mp32", q->id);
        break;
    case STREAM_Q_JAM_TEST:
        snprintf(params, sizeof(params), "&limit=1");
        break;
    case STREAM_Q_JAM_AUDIO:
        snprintf(params, sizeof(params), "&id=%s&audioformat=mp32", q->id);
        break;
    default:
        return false;
    }
    return jamendoUrl(method, client_id, params, out, size);
}

static bool archiveQueryUrl(const StreamQuery *q, char *out, size_t size)
{
    char words[STREAM_WORDS_MAX], query[STREAM_WORDS_MAX + 160];
    bool search = q->kind == STREAM_Q_IA_SEARCH_LIVE || q->kind == STREAM_Q_IA_SEARCH_FREE;
    if (search && !streamArchiveWords(q->words, words, sizeof(words)))
        return false;
    switch (q->kind) {
    case STREAM_Q_IA_POPULAR_LIVE: return archiveSearchUrl(Q_LIVE, STREAM_PAGE_SIZE, true, out, size);
    case STREAM_Q_IA_POPULAR_NETLABELS: return archiveSearchUrl(Q_NETLABELS, STREAM_PAGE_SIZE, true, out, size);
    case STREAM_Q_IA_TEST: return archiveSearchUrl(Q_LIVE, 1, false, out, size);
    case STREAM_Q_IA_SEARCH_LIVE:
    case STREAM_Q_IA_SEARCH_FREE:
        snprintf(query, sizeof(query), "%s AND (%s)", q->kind == STREAM_Q_IA_SEARCH_LIVE ? Q_LIVE : Q_FREE, words);
        return archiveSearchUrl(query, STREAM_PAGE_SIZE, false, out, size);
    case STREAM_Q_IA_ITEM: {
        if (!streamIdentifierValid(q->id))
            return false;
        int n = snprintf(out, size, ARCHIVE_METADATA "%s", q->id);
        return n > 0 && (size_t)n < size;
    }
    default: return false;
    }
}

bool streamQueryUrl(const StreamQuery *q, const char *client_id, char *out, size_t size)
{
    if (!q || q->kind < 0 || q->kind >= STREAM_Q_COUNT)
        return false;
    return streamQueryService(q->kind) == STREAM_JAMENDO ? jamendoQueryUrl(q, client_id, out, size)
                                                          : archiveQueryUrl(q, out, size);
}

bool streamArchiveFileUrl(const char *identifier, const char *file, char *out, size_t size)
{
    char encoded[UTIL_PATH_MAX];
    if (!streamIdentifierValid(identifier) || !file[0] || !encode(file, true, encoded, sizeof(encoded)))
        return false;
    int n = snprintf(out, size, ARCHIVE_DOWNLOAD "%s/%s", identifier, encoded);
    return n > 0 && (size_t)n < size;
}

// Jamendo's cover sizes: the smallest that's at least px.
static int jamendoWidth(int px)
{
    static const int SIZES[] = {100, 200, 300, 400, 500, STREAM_COVER_PX_MAX};
    for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); i++)
        if (SIZES[i] >= px)
            return SIZES[i];
    return STREAM_COVER_PX_MAX;
}

bool streamCoverUrl(const StreamTarget *cover, char *out, size_t size)
{
    int n = -1;
    if (cover->service == STREAM_ARCHIVE && streamIdentifierValid(cover->key))
        n = snprintf(out, size, ARCHIVE_IMAGE "%s", cover->key);
    else if (cover->service == STREAM_JAMENDO && cover->key[0] == 'a' && streamAllDigits(cover->key + 1, ID_DIGITS_MAX))
        n = snprintf(out, size, JAMENDO_COVERS "/?type=artist&id=%s&width=%d", cover->key + 1, jamendoWidth(cover->px));
    else if (cover->service == STREAM_JAMENDO && streamAllDigits(cover->key, ID_DIGITS_MAX))
        n = snprintf(out, size, JAMENDO_COVERS "/?type=album&id=%s&width=%d", cover->key, jamendoWidth(cover->px));
    return n > 0 && (size_t)n < size;
}

// --- Genres ---

static const char *const GENRE_TAGS[STREAM_GENRE_COUNT] = {
    "rock", "pop", "electronic", "hiphop", "jazz", "classical", "ambient", "metal",
    "folk", "reggae", "blues", "lounge", "soundtrack", "world", "latin", "punk",
};
static const char *const GENRE_NAMES[STREAM_GENRE_COUNT] = {
    N_("Rock"), N_("Pop"), N_("Electronic"), N_("Hip hop"), N_("Jazz"), N_("Classical"), N_("Ambient"), N_("Metal"),
    N_("Folk"), N_("Reggae"), N_("Blues"), N_("Lounge"), N_("Soundtrack"), N_("World"), N_("Latin"), N_("Punk"),
};

const char *streamGenreTag(int index)
{
    return index >= 0 && index < STREAM_GENRE_COUNT ? GENRE_TAGS[index] : "";
}

const char *streamGenreName(int index)
{
    return index >= 0 && index < STREAM_GENRE_COUNT ? T(GENRE_NAMES[index]) : "";
}
