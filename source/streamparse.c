// The Internet Archive's answers (JSON) into lists: stream.h's streamParseAnswer. They are untrusted:
// every text is bounded, and anything unexpected is left out.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "stream.h"
#include "stream_text.h"

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

// A member's text, or the first string of an array (an item's creators, its titles).
static void textOf(const JsonValue *v, const char *key, char *out, size_t size)
{
    const JsonValue *m = jsonGet(v, key);
    out[0] = '\0';
    if (m && m->type == JSON_ARRAY)
        for (m = m->child; m && m->type != JSON_STRING; m = m->next) {
        }
    if (m && m->type == JSON_STRING)
        streamCopyText(out, size, m->text);
}

// "1977-05-08T00:00:00Z" -> its first n characters, if it starts with a year.
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

static bool parseSearch(const JsonValue *root, StreamList *out, char *why, size_t why_size)
{
    const JsonValue *response = jsonGet(root, "response");
    const JsonValue *docs = jsonGet(response, "docs");
    if (!docs || docs->type != JSON_ARRAY) {
        char text[256], message[160];
        streamCopyText(message, sizeof(message), jsonText(root, "error"));
        if (message[0])
            snprintf(text, sizeof(text), T("The Internet Archive said: %s"), message);
        fail(why, why_size, message[0] ? text : T("The Internet Archive's answer wasn't what was expected."));
        return false;
    }
    long long found = jsonInt(response, "numFound");
    out->total = found >= 0 && found < 1000000000 ? (long)found : -1;
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
        if (!streamItemCoverLocation(id, item->location, sizeof(item->location)))
            item->location[0] = '\0';
        if (!item->title[0])
            snprintf(item->title, sizeof(item->title), "%s", id);
    }
    return true;
}

// The formats an item's songs are taken in, best first: one format for the whole item.
static const char *const FORMATS[] = {"VBR MP3", "MP3", "128Kbps MP3", "64Kbps MP3", "Ogg Vorbis", "Flac",
                                      "24bit Flac"};
#define FORMAT_COUNT ((int)(sizeof(FORMATS) / sizeof(FORMATS[0])))

static int formatRank(const JsonValue *file)
{
    const char *format = jsonText(file, "format");
    for (int i = 0; i < FORMAT_COUNT; i++)
        if (!strcmp(format, FORMATS[i]))
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

// Whether the item is in a collection (its "collection" is one name or a list of them).
static bool inCollection(const JsonValue *meta, const char *name)
{
    const JsonValue *c = jsonGet(meta, "collection");
    if (c && c->type == JSON_STRING)
        return !strcmp(c->text, name);
    for (c = c && c->type == JSON_ARRAY ? c->child : NULL; c; c = c->next)
        if (c->type == JSON_STRING && !strcmp(c->text, name))
            return true;
    return false;
}

// The item's license code: the one it declares, else what its collection says.
static void itemLicense(const JsonValue *meta, bool *live, char *out, size_t size)
{
    char url[256];
    textOf(meta, "licenseurl", url, sizeof(url));
    streamLicenseCode(url, out, size);
    *live = !strcmp(jsonText(meta, "mediatype"), "etree") || inCollection(meta, "etree");
    if (strcmp(out, STREAM_NO_LICENSE))
        return;
    if (*live)
        snprintf(out, size, STREAM_LMA_LICENSE);
    else if (inCollection(meta, "78rpm"))
        snprintf(out, size, STREAM_78_LICENSE);
}

// A file's song: its title (the file's name without folder and extension if none), creator and album.
static bool addSong(StreamList *out, const StreamQuery *q, const JsonValue *f, const char *license, bool live,
                    const char *album, const char *creator)
{
    const char *name = jsonText(f, "name");
    char location[UTIL_PATH_MAX];
    if (!streamSongLocation(q->id, license, name, location, sizeof(location)))
        return true;  // Left out.
    StreamItem *item = addItem(out);
    if (!item)
        return out->count >= STREAM_ITEMS_MAX;
    item->kind = STREAM_ITEM_TRACK;
    streamCopyText(item->id, sizeof(item->id), name);  // For the order; the location has all of it.
    textOf(f, "title", item->title, sizeof(item->title));
    textOf(f, "creator", item->artist, sizeof(item->artist));
    if (!item->artist[0] || live)
        snprintf(item->artist, sizeof(item->artist), "%s", creator);  // The band, not "O.A.R. (...)".
    snprintf(item->album, sizeof(item->album), "%s", album);
    item->position = trackNumber(f);
    snprintf(item->location, sizeof(item->location), "%s", location);
    if (!item->title[0]) {
        const char *slash = strrchr(name, '/');
        streamCopyText(item->title, sizeof(item->title), slash ? slash + 1 : name);
        char *dot = strrchr(item->title, '.');
        if (dot && dot != item->title)
            *dot = '\0';
    }
    return true;
}

static bool parseItem(const StreamQuery *q, const JsonValue *root, StreamList *out, char *why, size_t why_size)
{
    const JsonValue *meta = jsonGet(root, "metadata"), *files = jsonGet(root, "files");
    if (!root->child) {  // "{}": no such item (any more).
        fail(why, why_size, T("That item isn't on the Internet Archive any more."));
        return false;
    }
    if (!meta || meta->type != JSON_OBJECT || !files || files->type != JSON_ARRAY) {
        fail(why, why_size, T("The Internet Archive's answer wasn't what was expected."));
        return false;
    }
    char license[STREAM_LICENSE_MAX], album[STREAM_TEXT_MAX], creator[STREAM_TEXT_MAX];
    bool live;
    itemLicense(meta, &live, license, sizeof(license));
    textOf(meta, "title", album, sizeof(album));
    textOf(meta, "creator", creator, sizeof(creator));
    int best = FORMAT_COUNT;
    for (const JsonValue *f = files->child; f; f = f->next) {
        int rank = formatRank(f);
        if (rank >= 0 && rank < best && streamPlayableExtension(jsonText(f, "name")))
            best = rank;
    }
    for (const JsonValue *f = files->child; f && best < FORMAT_COUNT && out->count < STREAM_ITEMS_MAX; f = f->next) {
        if (formatRank(f) == best && !addSong(out, q, f, license, live, album, creator)) {
            fail(why, why_size, T("Not enough memory."));
            return false;
        }
    }
    if (out->count)  // No items: no array to sort.
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
        fail(why, why_size, T("The Internet Archive's answer wasn't what was expected."));
        ok = false;
    } else if (q->kind == STREAM_Q_ITEM) {
        ok = parseItem(q, root, out, why, why_size);
    } else {
        ok = parseSearch(root, out, why, why_size);
    }
    if (!ok) {
        streamListFree(out);
        if (!why[0])
            fail(why, why_size, T("The Internet Archive's answer wasn't what was expected."));
    }
    return ok;
}
