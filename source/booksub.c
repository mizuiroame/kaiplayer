#include "booksub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static void idOf(const JsonValue *v, const char *key, char *out, size_t size)
{
    const JsonValue *m = jsonGet(v, key);
    if (m && m->type == JSON_NUMBER)
        snprintf(out, size, "%lld", jsonInt(v, key));
    else
        snprintf(out, size, "%s", m && m->type == JSON_STRING ? m->text : "");
}

size_t bookSubGenreLists(const JsonValue *body, BookSubList *out, size_t max)
{
    const JsonValue *genres = jsonGet(body, "genres");
    size_t n = 0;
    for (const JsonValue *g = subFirst(genres, "genre"); g && n < max; g = subNext(genres, "genre", g)) {
        const char *name = jsonText(g, "value");
        char encoded[SUB_ID_MAX];
        if (!booksIsBookName(name) || !subEncode(name, encoded, sizeof(encoded)))
            continue;
        snprintf(out[n++].params, sizeof(out->params), "type=byGenre&genre=%s", encoded);
    }
    return n;
}

size_t bookSubFolderLists(const JsonValue *body, BookSubList *out, size_t max)
{
    const JsonValue *folders = jsonGet(body, "musicFolders");
    size_t n = 0;
    for (const JsonValue *f = subFirst(folders, "musicFolder"); f && n < max;
         f = subNext(folders, "musicFolder", f)) {
        char id[SUB_ID_MAX], encoded[SUB_ID_MAX];
        idOf(f, "id", id, sizeof(id));
        if (!id[0] || !booksIsBookName(jsonText(f, "name")) || !subEncode(id, encoded, sizeof(encoded)))
            continue;
        snprintf(out[n++].params, sizeof(out->params), "type=alphabeticalByName&musicFolderId=%s", encoded);
    }
    return n;
}

size_t bookSubAlbumIds(const JsonValue *body, char (*ids)[SUB_ID_MAX], size_t count, size_t max, size_t *page)
{
    const JsonValue *list = jsonGet(body, "albumList2");
    *page = 0;
    for (const JsonValue *a = subFirst(list, "album"); a; a = subNext(list, "album", a)) {
        (*page)++;
        char id[SUB_ID_MAX];
        idOf(a, "id", id, sizeof(id));
        bool known = !id[0];
        for (size_t i = 0; i < count && !known; i++)
            known = strcmp(ids[i], id) == 0;
        if (!known && count < max)
            snprintf(ids[count++], SUB_ID_MAX, "%s", id);
    }
    return count;
}

typedef struct {
    const JsonValue *song;
    int disc, track;
    size_t order;
} SongRef;

static int compareSongs(const void *x, const void *y)
{
    const SongRef *a = x, *b = y;
    if (a->disc != b->disc)
        return a->disc < b->disc ? -1 : 1;
    if (a->track != b->track)
        return a->track < b->track ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}

static const char *suffixOf(const JsonValue *song)
{
    const char *suffix = jsonText(song, "suffix");
    if (suffix[0])
        return suffix;
    const char *path = jsonText(song, "path"), *dot = strrchr(path, '.');
    return dot && !strchr(dot, '/') ? dot + 1 : "";
}

bool bookSubAlbum(const JsonValue *body, Book *out)
{
    memset(out, 0, sizeof(*out));
    const JsonValue *album = jsonGet(body, "album");
    char id[SUB_ID_MAX], key[SUB_ID_MAX + 16];
    idOf(album, "id", id, sizeof(id));
    if (!id[0])
        return false;
    size_t count = 0;
    for (const JsonValue *s = subFirst(album, "song"); s; s = subNext(album, "song", s))
        count++;
    SongRef *songs = count ? calloc(count, sizeof(*songs)) : NULL;
    if (!songs)
        return false;
    size_t n = 0;
    for (const JsonValue *s = subFirst(album, "song"); s && n < count; s = subNext(album, "song", s), n++)
        songs[n] = (SongRef){s, (int)jsonInt(s, "discNumber"), (int)jsonInt(s, "track"), n};
    qsort(songs, count, sizeof(*songs), compareSongs);

    snprintf(key, sizeof(key), BOOKSUB_KEY_PREFIX "%s", id);
    out->key = strdup(key);
    out->server = true;
    snprintf(out->title, sizeof(out->title), "%s", jsonText(album, "name"));
    const char *author = jsonText(album, "displayArtist");
    snprintf(out->author, sizeof(out->author), "%s", author[0] ? author : jsonText(album, "artist"));
    const char **titles = calloc(count, sizeof(*titles));
    bool ok = out->key && titles;
    size_t parts = 0;
    for (size_t i = 0; i < count && ok; i++) {
        const JsonValue *s = songs[i].song;
        char song_id[SUB_ID_MAX], cover[SUB_ID_MAX], location[UTIL_PATH_MAX];
        idOf(s, "id", song_id, sizeof(song_id));
        idOf(s, "coverArt", cover, sizeof(cover));
        if (!cover[0])
            idOf(album, "coverArt", cover, sizeof(cover));
        if (jsonGet(s, "isVideo") && jsonGet(s, "isVideo")->type == JSON_TRUE)
            continue;
        if (!subSongLocation(song_id, cover, suffixOf(s), location, sizeof(location)) || !utilIsAudioFile(location))
            continue;
        long long size = jsonInt(s, "size");
        titles[parts++] = jsonText(s, "title");
        ok = bookAddPart(out, location, size > 0 && size < 0x7FFFFFFF ? (long)size : 0, jsonNumber(s, "duration"));
    }
    if (ok && out->part_count > 1)
        bookFinishProbe(out, titles);
    else if (ok && out->part_count == 1)
        out->duration = out->parts[0].duration;  // Its chapters are read from the file.
    free(titles);
    free(songs);
    if (!ok || out->part_count == 0) {
        bookFree(out);
        return false;
    }
    if (!out->title[0])
        snprintf(out->title, sizeof(out->title), "%s", id);
    return true;
}

bool bookSubSongId(const char *location, char *out, size_t size)
{
    SubTarget t;
    if (!subParseLocation(location, &t) || t.cover)
        return false;
    snprintf(out, size, "%s", t.id);
    return true;
}

void bookSubApplyBookmarks(const JsonValue *body, BookList *l)
{
    const JsonValue *marks = jsonGet(body, "bookmarks");
    for (const JsonValue *m = subFirst(marks, "bookmark"); m; m = subNext(marks, "bookmark", m)) {
        char id[SUB_ID_MAX], encoded[SUB_ID_MAX];
        idOf(jsonGet(m, "entry"), "id", id, sizeof(id));
        if (!id[0] || !subEncode(id, encoded, sizeof(encoded)))
            continue;
        long long changed = bookSubTime(jsonText(m, "changed"));
        if (!changed)
            changed = bookSubTime(jsonText(m, "created"));
        double position = jsonNumber(m, "position") / 1000.0;
        for (size_t b = 0; b < l->count; b++) {
            Book *book = &l->items[b];
            for (size_t p = 0; book->server && p < book->part_count; p++) {
                char song[SUB_ID_MAX];
                if (!bookSubSongId(book->parts[p].path, song, sizeof(song)) || strcmp(song, encoded) != 0 ||
                    changed <= book->mark_time)
                    continue;
                book->mark_part = p;
                book->mark_position = position > 0.0 ? position : 0.0;
                book->mark_time = changed;
            }
        }
    }
}

// Days from 1970-01-01 to a date of the Gregorian calendar.
static long long daysFromCivil(long long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

long long bookSubTime(const char *iso)
{
    int y, mo, d, h, mi, s, used = 0;
    if (!iso || sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &s, &used) != 6 || mo < 1 || mo > 12 ||
        d < 1 || d > 31 || h > 23 || mi > 59 || s > 60)
        return 0;
    const char *rest = iso + used;
    if (*rest == '.')
        for (rest++; *rest >= '0' && *rest <= '9'; rest++) {
        }
    long long t = daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s;
    int oh, om;
    if ((*rest == '+' || *rest == '-') && sscanf(rest + 1, "%2d:%2d", &oh, &om) == 2)
        t -= (*rest == '+' ? 1 : -1) * (oh * 3600 + om * 60);
    return t;
}
