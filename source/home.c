#include "home.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "subsonic.h"

// A server's song location also holds its cover's id, which Navidrome changes
// when it scans again ("al-x_<when>"): a play is found by the song's own name
// ("<id>.<ext>") too.
typedef struct {
    const char *name;
    size_t song;
} NamedSong;

typedef struct {
    const Catalog *c;  // May be NULL.
    NamedSong *by_name;  // The catalog's server songs, by name; NULL if it has none.
    size_t count;
} SongLookup;

static bool isServerSong(const char *path)
{
    return strncmp(path, SUB_PREFIX, sizeof(SUB_PREFIX) - 1) == 0;
}

static const char *songName(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int byName(const void *a, const void *b)
{
    return strcmp(((const NamedSong *)a)->name, ((const NamedSong *)b)->name);
}

static void lookupInit(SongLookup *l, const Catalog *c)
{
    memset(l, 0, sizeof(*l));
    l->c = c;
    if (!c || c->song_count == 0 || !isServerSong(c->songs[0].path))
        return;
    l->by_name = malloc(c->song_count * sizeof(*l->by_name));
    if (!l->by_name)
        return;  // Exact locations only.
    for (size_t i = 0; i < c->song_count; i++) {
        if (isServerSong(c->songs[i].path))
            l->by_name[l->count++] = (NamedSong){songName(c->songs[i].path), i};
    }
    qsort(l->by_name, l->count, sizeof(*l->by_name), byName);
}

static void lookupFree(SongLookup *l)
{
    free(l->by_name);
    l->by_name = NULL;
    l->count = 0;
}

// The song a history entry names, or -1.
static long lookupFind(const SongLookup *l, const HistoryEntry *e)
{
    if (!l->c || e->kind != HISTORY_SONG)
        return -1;
    long s = catalogFind(l->c, e->key);
    if (s >= 0 || l->count == 0 || !isServerSong(e->key))
        return s;
    NamedSong key = {songName(e->key), 0};
    const NamedSong *found = bsearch(&key, l->by_name, l->count, sizeof(key), byName);
    return found ? (long)found->song : -1;
}

static bool contains(const HomeRow *row, size_t item)
{
    for (size_t i = 0; i < row->count; i++)
        if (row->items[i] == item)
            return true;
    return false;
}

static void addOnce(HomeRow *row, size_t item)
{
    if (row->count < HOME_ROW_MAX && !contains(row, item))
        row->items[row->count++] = item;
}

// Artists by play count, most first (ties: most recent first).
static void topArtists(Home *home, const SongLookup *l, const History *h)
{
    const Catalog *c = l->c;
    size_t *plays = calloc(c->artist_count ? c->artist_count : 1, sizeof(size_t));
    size_t *last = calloc(c->artist_count ? c->artist_count : 1, sizeof(size_t));
    if (!plays || !last) {
        free(plays);
        free(last);
        return;
    }
    for (size_t i = 0; i < h->count; i++) {
        long s = lookupFind(l, &h->items[i]);
        if (s < 0)
            continue;
        size_t artist = c->albums[c->songs[s].album_index].artist;
        plays[artist]++;
        last[artist] = i + 1;
    }
    HomeRow *row = &home->rows[HOME_TOP_ARTISTS];
    while (row->count < HOME_ROW_MAX) {
        size_t best = c->artist_count;
        for (size_t a = 0; a < c->artist_count; a++) {
            if (plays[a] && (best == c->artist_count || plays[a] > plays[best] ||
                             (plays[a] == plays[best] && last[a] > last[best])))
                best = a;
        }
        if (best == c->artist_count)
            break;
        row->items[row->count++] = best;
        plays[best] = 0;
    }
    free(plays);
    free(last);
}

void homeCompute(Home *home, const Catalog *c, const History *h)
{
    memset(home, 0, sizeof(*home));
    SongLookup lookup;
    lookupInit(&lookup, c);
    for (size_t i = h->count; i-- > 0;) {  // Newest first.
        const HistoryEntry *e = &h->items[i];
        if (e->kind == HISTORY_PLAYLIST) {
            bool seen = false;
            for (size_t k = 0; k < home->playlist_count && !seen; k++)
                seen = strcmp(home->playlists[k], e->key) == 0;
            if (!seen && home->playlist_count < HOME_ROW_MAX)
                snprintf(home->playlists[home->playlist_count++], PLAYLIST_NAME_MAX + 1, "%s", e->key);
            continue;
        }
        long s = lookupFind(&lookup, e);
        if (s < 0)
            continue;  // Not in the music folder (any more).
        size_t album = c->songs[s].album_index;
        addOnce(&home->rows[HOME_RECENT_ALBUMS], album);
        addOnce(&home->rows[HOME_RECENT_ARTISTS], c->albums[album].artist);
    }
    if (!c)
        return;
    topArtists(home, &lookup, h);
    lookupFree(&lookup);
    // Nothing played yet: show some of the library instead of an empty row.
    if (home->rows[HOME_RECENT_ALBUMS].count == 0) {
        home->albums_from_library = c->album_count > 0;
        for (size_t a = 0; a < c->album_count && a < HOME_ROW_MAX; a++)
            home->rows[HOME_RECENT_ALBUMS].items[home->rows[HOME_RECENT_ALBUMS].count++] = a;
    }
}

const HomeRowKind HOME_GRID[HOME_ROW_COUNT] = {
    HOME_TOP_ARTISTS, HOME_RECENT_PLAYLISTS, HOME_RECENT_ALBUMS, HOME_RECENT_ARTISTS,
};

int homeGridPlace(HomeRowKind row)
{
    for (int i = 0; i < HOME_ROW_COUNT; i++)
        if (HOME_GRID[i] == row)
            return i;
    return 0;
}

size_t homeRowCount(const Home *home, HomeRowKind row)
{
    if (row == HOME_RECENT_PLAYLISTS)
        return home->playlist_count;
    return row < HOME_RECENT_PLAYLISTS ? home->rows[row].count : 0;
}
