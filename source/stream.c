#include "stream.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lang.h"
#include "stream_text.h"

#define ARCHIVE_SEARCH "https://archive.org/advancedsearch.php"
#define ARCHIVE_METADATA "https://archive.org/metadata/"
#define ARCHIVE_DOWNLOAD "https://archive.org/download/"
#define ARCHIVE_IMAGE "https://archive.org/services/img/"
#define ARCHIVE_PAGE "archive.org/details/"
#define CC_HOST "creativecommons.org/"
#define COVER_FOLDER "cover"
#define IDENTIFIER_MAX 100
#define COVER_PX_MAX 4096
#define DOT "\xC2\xB7"  // The middle dot between a credit's parts.

// What is searched: only what has a clear permission (STREAMING-STATUS.md).
#define Q_LIVE "collection:etree AND mediatype:etree"
#define Q_78 "collection:78rpm AND mediatype:audio"
#define Q_CLASSICAL "mediatype:audio AND licenseurl:*publicdomain* AND subject:classical AND NOT collection:librivoxaudio"
#define Q_FREE "mediatype:audio AND licenseurl:*creativecommons* AND NOT collection:librivoxaudio"
#define ARCHIVE_FIELDS "&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=creator&fl%5B%5D=date&fl%5B%5D=licenseurl"
#define BY_DOWNLOADS "&sort%5B%5D=downloads+desc"

bool streamIsLocation(const char *path)
{
    return path && !strncmp(path, STREAM_PREFIX, sizeof(STREAM_PREFIX) - 1);
}

// --- Small helpers ---

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

bool streamSongLocation(const char *identifier, const char *license, const char *file, char *out, size_t size)
{
    char encoded[UTIL_PATH_MAX];
    if (!streamIdentifierValid(identifier) || !licenseValid(license) || !streamPlayableExtension(file) ||
        strlen(file) >= STREAM_FILE_MAX || !encode(file, false, encoded, sizeof(encoded)))
        return false;
    for (const char *p = file; *p; p++)
        if ((unsigned char)*p < 0x20)
            return false;
    int n = snprintf(out, size, STREAM_PREFIX "%s/%s/%s", identifier, license, encoded);
    return n > 0 && (size_t)n < size;
}

bool streamItemCoverLocation(const char *identifier, char *out, size_t size)
{
    int n = streamIdentifierValid(identifier) ? snprintf(out, size, STREAM_PREFIX "%s/" COVER_FOLDER "/0.jpg", identifier)
                                              : -1;
    return n > 0 && (size_t)n < size;
}

bool streamCoverLocation(const char *location, int px, char *out, size_t size)
{
    StreamTarget t;
    if (!streamParseLocation(location, &t) || px <= 0 || px > COVER_PX_MAX)
        return false;
    int n = snprintf(out, size, STREAM_PREFIX "%s/" COVER_FOLDER "/%d.jpg", t.identifier, px);
    return n > 0 && (size_t)n < size;
}

// "a/b/c" after the prefix: its three parts (the third is the rest).
static bool splitLocation(const char *rest, char *first, size_t first_size, char *second, size_t second_size,
                          const char **third)
{
    const char *a = strchr(rest, '/');
    if (!a || a == rest || (size_t)(a - rest) >= first_size)
        return false;
    const char *b = strchr(a + 1, '/');
    if (!b || b == a + 1 || (size_t)(b - a - 1) >= second_size || !b[1])
        return false;
    memcpy(first, rest, (size_t)(a - rest));
    first[a - rest] = '\0';
    memcpy(second, a + 1, (size_t)(b - a - 1));
    second[b - a - 1] = '\0';
    *third = b + 1;
    return true;
}

// "300.jpg" -> 300 (0 to 4096: 0 is a row's key).
static bool coverPx(const char *name, int *px)
{
    size_t digits = strspn(name, "0123456789");
    if (!digits || digits > 4 || strcmp(name + digits, ".jpg"))
        return false;
    *px = atoi(name);
    return *px <= COVER_PX_MAX;
}

bool streamParseLocation(const char *location, StreamTarget *out)
{
    memset(out, 0, sizeof(*out));
    if (!streamIsLocation(location) || strlen(location) >= UTIL_PATH_MAX)
        return false;
    char second[STREAM_LICENSE_MAX];
    const char *third;
    if (!splitLocation(location + sizeof(STREAM_PREFIX) - 1, out->identifier, sizeof(out->identifier), second,
                       sizeof(second), &third) ||
        !streamIdentifierValid(out->identifier))
        return false;
    if (!strcmp(second, COVER_FOLDER)) {
        out->cover = true;
        return coverPx(third, &out->px);
    }
    if (!licenseValid(second))
        return false;
    snprintf(out->license, sizeof(out->license), "%s", second);
    return !strchr(third, '/') && decode(third, strlen(third), out->file, sizeof(out->file)) &&
           streamPlayableExtension(out->file);
}

// --- Licenses and credits ---

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
    snprintf(out, size, "%s", STREAM_NO_LICENSE);
    const char *host = license_url ? strstr(license_url, CC_HOST) : NULL;
    const char *p = host ? host + sizeof(CC_HOST) - 1 : NULL;
    char kind[16], name[16], version[8] = "";
    if (!p || !segment(&p, kind, sizeof(kind)) || !segment(&p, name, sizeof(name)))
        return;
    bool has_version = segment(&p, version, sizeof(version)) && isdigit((unsigned char)version[0]) &&
                       strspn(version, "0123456789.") == strlen(version);
    const char *dash = has_version ? "-" : "", *v = has_version ? version : "";
    char code[STREAM_LICENSE_MAX];
    if (!strcmp(kind, "licenses") && !strcmp(name, "publicdomain"))
        snprintf(code, sizeof(code), "pd");
    else if (!strcmp(kind, "publicdomain") && !strcmp(name, "zero"))
        snprintf(code, sizeof(code), "cc0%s%s", dash, v);
    else if (!strcmp(kind, "publicdomain") && !strcmp(name, "mark"))
        snprintf(code, sizeof(code), "pdm%s%s", dash, v);
    else if (!strcmp(kind, "licenses") && !strncmp(name, "by", 2) &&
             strspn(name, "abcdefghijklmnopqrstuvwxyz-") == strlen(name))
        snprintf(code, sizeof(code), "%s%s%s", name, dash, v);
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
    if (!code || !licenseValid(code) || !strcmp(code, STREAM_NO_LICENSE))
        return;
    if (!strcmp(code, STREAM_LMA_LICENSE)) {
        snprintf(out, size, "%s", T("Live Music Archive: non-commercial"));
        return;
    }
    if (!strcmp(code, STREAM_78_LICENSE)) {
        snprintf(out, size, "%s", T("78 rpm collection"));
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
    if (!code || !licenseValid(code) || !strcmp(code, STREAM_NO_LICENSE))
        return;
    if (!strcmp(code, STREAM_LMA_LICENSE) || !strcmp(code, STREAM_78_LICENSE)) {
        snprintf(out, size, ARCHIVE_PAGE "%s", !strcmp(code, STREAM_LMA_LICENSE) ? "etree" : "78rpm");
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

bool streamPageLink(const char *location, char *out, size_t size)
{
    StreamTarget t;
    if (!streamParseLocation(location, &t))
        return false;
    int n = snprintf(out, size, ARCHIVE_PAGE "%s", t.identifier);
    return n > 0 && (size_t)n < size;
}

bool streamCreditLine(const char *location, char *out, size_t size)
{
    StreamTarget t;
    char license[96], page[160];
    if (!streamParseLocation(location, &t) || t.cover || !streamPageLink(location, page, sizeof(page)))
        return false;
    streamLicenseName(t.license, license, sizeof(license));
    snprintf(out, size, "%s%sInternet Archive  " DOT "  %s", license, license[0] ? "  " DOT "  " : "", page);
    return true;
}

// --- Requests ---

bool streamSearchWords(const char *typed, char *out, size_t size)
{
    if (!size)
        return false;
    char words[STREAM_WORDS_MAX * 2];
    size_t n = 0;
    bool space = true;  // No space first.
    for (const unsigned char *p = (const unsigned char *)typed; *p && n + 1 < sizeof(words); p++) {
        if (isalnum(*p) || *p >= 0x80) {
            words[n++] = (char)tolower(*p);
            space = false;
        } else if (!space) {
            words[n++] = ' ';
            space = true;
        }
    }
    words[n] = '\0';
    streamCopyText(out, size, words);  // A character cut at the end of the room goes.
    n = strlen(out);
    while (n && out[n - 1] == ' ')
        out[--n] = '\0';
    return n > 0;
}

static bool searchUrl(const char *query, bool by_downloads, char *out, size_t size)
{
    char q[STREAM_URL_MAX / 2];
    if (!encode(query, false, q, sizeof(q)))
        return false;
    int n = snprintf(out, size, ARCHIVE_SEARCH "?q=%s%s%s&rows=%d&page=1&output=json", q, ARCHIVE_FIELDS,
                     by_downloads ? BY_DOWNLOADS : "", STREAM_PAGE_SIZE);
    return n > 0 && (size_t)n < size;
}

bool streamQueryUrl(const StreamQuery *q, char *out, size_t size)
{
    char words[STREAM_WORDS_MAX], query[STREAM_WORDS_MAX + 160];
    if (!q)
        return false;
    bool search = q->kind == STREAM_Q_SEARCH_LIVE || q->kind == STREAM_Q_SEARCH_78 || q->kind == STREAM_Q_SEARCH_FREE;
    if (search && !streamSearchWords(q->words, words, sizeof(words)))
        return false;
    switch (q->kind) {
    case STREAM_Q_POPULAR_LIVE: return searchUrl(Q_LIVE, true, out, size);
    case STREAM_Q_POPULAR_78: return searchUrl(Q_78, true, out, size);
    case STREAM_Q_CLASSICAL: return searchUrl(Q_CLASSICAL, true, out, size);
    case STREAM_Q_SEARCH_LIVE:
    case STREAM_Q_SEARCH_78:
    case STREAM_Q_SEARCH_FREE: {
        const char *base = q->kind == STREAM_Q_SEARCH_LIVE ? Q_LIVE : q->kind == STREAM_Q_SEARCH_78 ? Q_78 : Q_FREE;
        snprintf(query, sizeof(query), "%s AND (%s)", base, words);
        return searchUrl(query, false, out, size);
    }
    case STREAM_Q_ITEM: {
        if (!streamIdentifierValid(q->id))
            return false;
        int n = snprintf(out, size, ARCHIVE_METADATA "%s", q->id);
        return n > 0 && (size_t)n < size;
    }
    default: return false;
    }
}

bool streamFileUrl(const StreamTarget *song, char *out, size_t size)
{
    char encoded[UTIL_PATH_MAX];
    if (song->cover || !streamIdentifierValid(song->identifier) || !song->file[0] ||
        !encode(song->file, true, encoded, sizeof(encoded)))
        return false;
    int n = snprintf(out, size, ARCHIVE_DOWNLOAD "%s/%s", song->identifier, encoded);
    return n > 0 && (size_t)n < size;
}

bool streamCoverUrl(const StreamTarget *cover, char *out, size_t size)
{
    int n = streamIdentifierValid(cover->identifier) ? snprintf(out, size, ARCHIVE_IMAGE "%s", cover->identifier) : -1;
    return n > 0 && (size_t)n < size;
}

// --- The first page ---

typedef struct {
    const char *label;
    StreamQueryKind kind;
    bool search;
} MenuRow;

// What the Archive is best for: concerts, old records, classical music, then anything free.
static const MenuRow MENU[] = {
    {N_("Popular live concerts"), STREAM_Q_POPULAR_LIVE, false},
    {N_("Search live concerts"), STREAM_Q_SEARCH_LIVE, true},
    {N_("Popular 78 rpm records"), STREAM_Q_POPULAR_78, false},
    {N_("Search 78 rpm records"), STREAM_Q_SEARCH_78, true},
    {N_("Public-domain classical music"), STREAM_Q_CLASSICAL, false},
    {N_("Search free music"), STREAM_Q_SEARCH_FREE, true},
};
#define MENU_COUNT ((int)(sizeof(MENU) / sizeof(MENU[0])))

int streamMenuCount(void)
{
    return MENU_COUNT;
}

const char *streamMenuLabel(int row)
{
    return row >= 0 && row < MENU_COUNT ? T(MENU[row].label) : "";
}

StreamQueryKind streamMenuQuery(int row)
{
    return row >= 0 && row < MENU_COUNT ? MENU[row].kind : STREAM_Q_COUNT;
}

bool streamMenuSearches(int row)
{
    return row >= 0 && row < MENU_COUNT && MENU[row].search;
}
