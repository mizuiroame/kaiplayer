#include "home.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static void topArtists(Home *home, const Catalog *c, const History *h)
{
    size_t *plays = calloc(c->artist_count ? c->artist_count : 1, sizeof(size_t));
    size_t *last = calloc(c->artist_count ? c->artist_count : 1, sizeof(size_t));
    if (!plays || !last) {
        free(plays);
        free(last);
        return;
    }
    for (size_t i = 0; i < h->count; i++) {
        long s = h->items[i].kind == HISTORY_SONG ? catalogFind(c, h->items[i].key) : -1;
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
        long s = c ? catalogFind(c, e->key) : -1;
        if (s < 0)
            continue;  // Not in the music folder (any more).
        size_t album = c->songs[s].album_index;
        addOnce(&home->rows[HOME_RECENT_ALBUMS], album);
        addOnce(&home->rows[HOME_RECENT_ARTISTS], c->albums[album].artist);
    }
    if (!c)
        return;
    topArtists(home, c, h);
    // Nothing played yet: show some of the library instead of an empty row.
    if (home->rows[HOME_RECENT_ALBUMS].count == 0) {
        home->albums_from_library = c->album_count > 0;
        for (size_t a = 0; a < c->album_count && a < HOME_ROW_MAX; a++)
            home->rows[HOME_RECENT_ALBUMS].items[home->rows[HOME_RECENT_ALBUMS].count++] = a;
    }
}

size_t homeRowCount(const Home *home, HomeRowKind row)
{
    if (row == HOME_RECENT_PLAYLISTS)
        return home->playlist_count;
    return row < HOME_RECENT_PLAYLISTS ? home->rows[row].count : 0;
}
