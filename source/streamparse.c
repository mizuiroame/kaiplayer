// The services' answers (JSON) into lists: stream.h's streamParseAnswer and streamJamendoAudioUrl.
// They are untrusted: every text is bounded, and anything unexpected is left out.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lang.h"
#include "stream.h"
#include "stream_text.h"

#define NO_LICENSE "-"
#define LMA_LICENSE "lma"
#define ID_DIGITS_MAX 20

// --- Answers ---

void streamListFree(StreamList *list)
{
    free(list->items);
    *list = (StreamList){.total = -1};
}

static StreamItem *addItem(StreamList *list)
{
    if (list->count >= STREAM_ITEMS_MAX)
        return NULL;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 16;
        StreamItem *grown = realloc(list->items, capacity * sizeof(StreamItem));
        if (!grown)
            return NULL;
        list->items = grown;
        list->capacity = capacity;
    }
    StreamItem *item = &list->items[list->count++];
    memset(item, 0, sizeof(*item));
    return item;
}

// A member's text, an id given as a number too, or the first string of an array (the Archive's creators).
static void textOf(const JsonValue *v, const char *key, char *out, size_t size)
{
    const JsonValue *m = jsonGet(v, key);
    out[0] = '\0';
    if (m && m->type == JSON_ARRAY)
        for (m = m->child; m && m->type != JSON_STRING; m = m->next) {
        }
    if (m && m->type == JSON_STRING)
        streamCopyText(out, size, m->text);
    else if (m && m->type == JSON_NUMBER && m->number >= 0 && m->number < 1e18)
        snprintf(out, size, "%.0f", m->number);
}

// "2015-03-02..." -> its first n characters, if it starts with a year.
static void dateOf(const JsonValue *v, const char *key, size_t n, char *out, size_t size)
{
    char text[STREAM_DETAIL_MAX];
    textOf(v, key, text, sizeof(text));
    out[0] = '\0';
    bool year = strlen(text) >= 4;
    for (int i = 0; i < 4 && year; i++)
        year = isdigit((unsigned char)text[i]) != 0;
    if (year)
        snprintf(out, size, "%.*s", (int)(n < size ? n : size - 1), text);
}

static void fail(char *why, size_t why_size, const char *text)
{
    streamCopyText(why, why_size, text);
}

static bool jamendoTrack(const JsonValue *r, StreamList *out)
{
    char id[STREAM_ID_MAX], album_id[STREAM_ID_MAX], license[STREAM_URL_MAX / 8], location[UTIL_PATH_MAX];
    textOf(r, "id", id, sizeof(id));
    textOf(r, "album_id", album_id, sizeof(album_id));
    textOf(r, "license_ccurl", license, sizeof(license));
    if (!streamJamendoLocation(album_id, license, id, location, sizeof(location)))
        return true;  // Left out.
    StreamItem *item = addItem(out);
    if (!item)
        return out->count >= STREAM_ITEMS_MAX;
    item->kind = STREAM_ITEM_TRACK;
    snprintf(item->id, sizeof(item->id), "%s", id);
    textOf(r, "name", item->title, sizeof(item->title));
    textOf(r, "artist_name", item->artist, sizeof(item->artist));
    textOf(r, "album_name", item->album, sizeof(item->album));
    dateOf(r, "releasedate", 4, item->detail, sizeof(item->detail));
    long long position = jsonInt(r, "position");
    item->position = position > 0 && position < 10000 ? (int)position : 0;
    snprintf(item->location, sizeof(item->location), "%s", location);
    if (!item->title[0])
        snprintf(item->title, sizeof(item->title), "%s", id);
    return true;
}

static bool jamendoAlbumOrArtist(const JsonValue *r, bool artist, StreamList *out)
{
    char id[STREAM_ID_MAX], key[STREAM_ID_MAX + 1];
    textOf(r, "id", id, sizeof(id));
    if (!streamAllDigits(id, ID_DIGITS_MAX))
        return true;
    StreamItem *item = addItem(out);
    if (!item)
        return out->count >= STREAM_ITEMS_MAX;
    item->kind = artist ? STREAM_ITEM_ARTIST : STREAM_ITEM_ALBUM;
    snprintf(item->id, sizeof(item->id), "%s", id);
    textOf(r, "name", item->title, sizeof(item->title));
    if (!artist) {
        textOf(r, "artist_name", item->artist, sizeof(item->artist));
        dateOf(r, "releasedate", 4, item->detail, sizeof(item->detail));
    }
    snprintf(key, sizeof(key), "%s%s", artist ? "a" : "", id);
    if (!streamCoverKeyLocation(STREAM_JAMENDO, key, item->location, sizeof(item->location)))
        item->location[0] = '\0';
    if (!item->title[0])
        snprintf(item->title, sizeof(item->title), "%s", id);
    return true;
}

// Jamendo's "headers": true if it says it worked; otherwise why.
static bool jamendoHeaders(const JsonValue *root, char *why, size_t why_size)
{
    const JsonValue *h = jsonGet(root, "headers");
    if (!h || h->type != JSON_OBJECT || !jsonGet(root, "results")) {
        fail(why, why_size, T("Jamendo's answer wasn't what was expected."));
        return false;
    }
    if (!strcmp(jsonText(h, "status"), "success"))
        return true;
    long long code = jsonInt(h, "code");
    if (code == 5)
        fail(why, why_size, T("Jamendo didn't take the client ID: check it in Settings, Online music."));
    else if (code == 11)
        fail(why, why_size, T("Jamendo has suspended this client ID."));
    else {
        char message[160], text[256];
        streamCopyText(message, sizeof(message), jsonText(h, "error_message"));
        snprintf(text, sizeof(text), T("Jamendo said: %s"), message[0] ? message : "?");
        fail(why, why_size, text);
    }
    return false;
}

static bool parseJamendo(const StreamQuery *q, const JsonValue *root, StreamList *out, char *why, size_t why_size)
{
    if (!jamendoHeaders(root, why, why_size))
        return false;
    const JsonValue *results = jsonGet(root, "results");
    if (results->type != JSON_ARRAY) {
        fail(why, why_size, T("Jamendo's answer wasn't what was expected."));
        return false;
    }
    long long count = jsonInt(jsonGet(root, "headers"), "results_count");
    out->total = count >= 0 && count < 100000000 ? (long)count : -1;
    bool albums = q->kind == STREAM_Q_JAM_NEW_ALBUMS || q->kind == STREAM_Q_JAM_SEARCH_ALBUMS ||
                  q->kind == STREAM_Q_JAM_ARTIST_ALBUMS;
    bool artists = q->kind == STREAM_Q_JAM_SEARCH_ARTISTS;
    for (const JsonValue *r = results->child; r; r = r->next) {
        if (r->type != JSON_OBJECT)
            continue;
        bool ok = albums || artists ? jamendoAlbumOrArtist(r, artists, out) : jamendoTrack(r, out);
        if (!ok) {
            fail(why, why_size, T("Not enough memory."));
            return false;
        }
        if (out->count >= STREAM_ITEMS_MAX)
            break;
    }
    return true;
}

bool streamJamendoAudioUrl(const JsonValue *root, char *out, size_t size)
{
    char why[8];
    const JsonValue *results = jsonGet(root, "results");
    if (!jamendoHeaders(root, why, sizeof(why)) || !results || results->type != JSON_ARRAY || !results->child)
        return false;
    const char *audio = jsonText(results->child, "audio");
    size_t n = strlen(audio);
    if (strncmp(audio, "https://", 8) || n >= size || n < 9)
        return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)audio[i] <= 0x20 || audio[i] == 0x7F)
            return false;
    memcpy(out, audio, n + 1);
    return true;
}

static bool parseArchiveSearch(const StreamQuery *q, const JsonValue *root, StreamList *out, char *why,
                               size_t why_size)
{
    const JsonValue *response = jsonGet(root, "response");
    const JsonValue *docs = jsonGet(response, "docs");
    if (!docs || docs->type != JSON_ARRAY) {
        const char *error = jsonText(root, "error");
        char text[256], message[160];
        streamCopyText(message, sizeof(message), error);
        if (message[0])
            snprintf(text, sizeof(text), T("The Internet Archive said: %s"), message);
        fail(why, why_size, message[0] ? text : T("The Internet Archive's answer wasn't what was expected."));
        return false;
    }
    long long found = jsonInt(response, "numFound");
    out->total = found >= 0 && found < 1000000000 ? (long)found : -1;
    out->number = (int)(out->total > 0 && out->total < 2000000000 ? out->total : 0);
    if (q->kind == STREAM_Q_IA_TEST)
        return true;
    for (const JsonValue *d = docs->child; d && out->count < STREAM_ITEMS_MAX; d = d->next) {
        char id[STREAM_ID_MAX];
        textOf(d, "identifier", id, sizeof(id));
        if (d->type != JSON_OBJECT || !streamIdentifierValid(id))
            continue;
        StreamItem *item = addItem(out);
        if (!item) {
            fail(why, why_size, T("Not enough memory."));
            return false;
        }
        item->kind = STREAM_ITEM_ARCHIVE;
        snprintf(item->id, sizeof(item->id), "%s", id);
        textOf(d, "title", item->title, sizeof(item->title));
        textOf(d, "creator", item->artist, sizeof(item->artist));
        dateOf(d, "date", 10, item->detail, sizeof(item->detail));
        if (!streamCoverKeyLocation(STREAM_ARCHIVE, id, item->location, sizeof(item->location)))
            item->location[0] = '\0';
        if (!item->title[0])
            snprintf(item->title, sizeof(item->title), "%s", id);
    }
    return true;
}

// The formats an item's songs are taken in, best first: one format for the whole item.
static const char *const ARCHIVE_FORMATS[] = {"VBR MP3", "MP3", "128Kbps MP3", "64Kbps MP3", "Ogg Vorbis",
                                              "Flac", "24bit Flac"};
#define ARCHIVE_FORMAT_COUNT ((int)(sizeof(ARCHIVE_FORMATS) / sizeof(ARCHIVE_FORMATS[0])))

static int formatRank(const JsonValue *file)
{
    const char *format = jsonText(file, "format");
    for (int i = 0; i < ARCHIVE_FORMAT_COUNT; i++)
        if (!strcmp(format, ARCHIVE_FORMATS[i]))
            return i;
    return -1;
}

// "3", "03/12": 3; 0 if none.
static int trackNumber(const JsonValue *file)
{
    char text[16];
    textOf(file, "track", text, sizeof(text));
    long n = strtol(text, NULL, 10);
    return n > 0 && n < 10000 ? (int)n : 0;
}

static int byTrack(const void *a, const void *b)
{
    const StreamItem *x = a, *y = b;
    if (x->position != y->position)
        return x->position == 0 ? 1 : y->position == 0 ? -1 : x->position - y->position;
    return strcmp(x->id, y->id);  // The file's name.
}

// Whether the item is the Live Music Archive's (its license is the band's: non-commercial).
static bool liveMusic(const JsonValue *meta)
{
    if (!strcmp(jsonText(meta, "mediatype"), "etree"))
        return true;
    const JsonValue *c = jsonGet(meta, "collection");
    if (c && c->type == JSON_STRING)
        return !strcmp(c->text, "etree");
    for (c = c && c->type == JSON_ARRAY ? c->child : NULL; c; c = c->next)
        if (c->type == JSON_STRING && !strcmp(c->text, "etree"))
            return true;
    return false;
}

static bool parseArchiveItem(const StreamQuery *q, const JsonValue *root, StreamList *out, char *why,
                             size_t why_size)
{
    const JsonValue *meta = jsonGet(root, "metadata"), *files = jsonGet(root, "files");
    if (root->type == JSON_OBJECT && !root->child) {
        fail(why, why_size, T("That item isn't on the Internet Archive any more."));
        return false;
    }
    if (!meta || meta->type != JSON_OBJECT || !files || files->type != JSON_ARRAY) {
        fail(why, why_size, T("The Internet Archive's answer wasn't what was expected."));
        return false;
    }
    char license_url[256], license[STREAM_LICENSE_MAX], album[STREAM_TEXT_MAX], creator[STREAM_TEXT_MAX];
    textOf(meta, "licenseurl", license_url, sizeof(license_url));
    streamLicenseCode(license_url, license, sizeof(license));
    if (!strcmp(license, NO_LICENSE) && liveMusic(meta))
        snprintf(license, sizeof(license), LMA_LICENSE);
    textOf(meta, "title", album, sizeof(album));
    textOf(meta, "creator", creator, sizeof(creator));
    int best = ARCHIVE_FORMAT_COUNT;
    for (const JsonValue *f = files->child; f; f = f->next) {
        int rank = formatRank(f);
        if (rank >= 0 && rank < best && streamPlayableExtension(jsonText(f, "name")))
            best = rank;
    }
    for (const JsonValue *f = files->child; f && best < ARCHIVE_FORMAT_COUNT; f = f->next) {
        const char *name = jsonText(f, "name");
        char location[UTIL_PATH_MAX];
        if (formatRank(f) != best || !streamArchiveLocation(q->id, license, name, location, sizeof(location)))
            continue;
        StreamItem *item = addItem(out);
        if (!item) {
            if (out->count >= STREAM_ITEMS_MAX)
                break;
            fail(why, why_size, T("Not enough memory."));
            return false;
        }
        item->kind = STREAM_ITEM_TRACK;
        streamCopyText(item->id, sizeof(item->id), name);  // For the order; the location has all of it.
        textOf(f, "title", item->title, sizeof(item->title));
        textOf(f, "creator", item->artist, sizeof(item->artist));
        if (!item->artist[0] || liveMusic(meta))
            snprintf(item->artist, sizeof(item->artist), "%s", creator);  // The band, not "O.A.R. (...)".
        snprintf(item->album, sizeof(item->album), "%s", album);
        item->position = trackNumber(f);
        snprintf(item->location, sizeof(item->location), "%s", location);
        if (!item->title[0]) {  // The file's name, without its folder and extension.
            const char *slash = strrchr(name, '/');
            streamCopyText(item->title, sizeof(item->title), slash ? slash + 1 : name);
            char *dot = strrchr(item->title, '.');
            if (dot && dot != item->title)
                *dot = '\0';
        }
    }
    qsort(out->items, out->count, sizeof(StreamItem), byTrack);
    out->total = (long)out->count;
    return true;
}

bool streamParseAnswer(const StreamQuery *q, const JsonValue *root, StreamList *out, char *why, size_t why_size)
{
    streamListFree(out);
    why[0] = '\0';
    bool ok;
    if (!root || root->type != JSON_OBJECT) {
        fail(why, why_size, T("The answer wasn't what was expected."));
        ok = false;
    } else if (streamQueryService(q->kind) == STREAM_JAMENDO) {
        ok = parseJamendo(q, root, out, why, why_size);
    } else if (q->kind == STREAM_Q_IA_ITEM) {
        ok = parseArchiveItem(q, root, out, why, why_size);
    } else {
        ok = parseArchiveSearch(q, root, out, why, why_size);
    }
    if (!ok) {
        streamListFree(out);
        if (!why[0])
            fail(why, why_size, T("The answer wasn't what was expected."));
    }
    return ok;
}

