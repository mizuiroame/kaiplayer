// The Library tab's page stack and the Home rows, shared by input and drawing.
#include <stdio.h>
#include <string.h>

#include "app.h"

LibraryPage *appLibraryPage(App *app)
{
    LibraryTab *t = &app->library;
    if (t->depth == 0)
        t->stack[t->depth++] = (LibraryPage){.level = LIB_ROOT};
    return &t->stack[t->depth - 1];
}

size_t appLibraryCount(const App *app, const Catalog *c)
{
    const LibraryTab *t = &app->library;
    if (t->depth == 0)
        return LIB_ROOT_ROWS;
    const LibraryPage *p = &t->stack[t->depth - 1];
    switch (p->level) {
    case LIB_ROOT: return LIB_ROOT_ROWS;
    case LIB_ARTISTS: return c ? c->artist_count : 0;
    case LIB_ARTIST: return c && p->arg < c->artist_count ? c->artists[p->arg].album_count : 0;
    case LIB_ALBUMS: return c ? c->album_count : 0;
    case LIB_ALBUM: return c && p->arg < c->album_count ? c->albums[p->arg].song_count : 0;
    case LIB_SONGS: return c ? c->song_count : 0;
    default: return 0;
    }
}

// Recomputes the rows when the catalog or the history changed, and keeps
// the selection inside them.
const Home *appHome(App *app, const Catalog *c)
{
    unsigned generation = c ? c->generation : 0;
    if (generation != app->home_generation || app->history.count != app->home_history || !app->home_ready) {
        homeCompute(&app->home, c, &app->history);
        app->home_generation = generation;
        app->home_history = app->history.count;
        app->home_ready = true;
    }
    for (int r = 0; r < HOME_ROW_COUNT; r++) {
        size_t count = homeRowCount(&app->home, (HomeRowKind)r);
        if (app->home_col[r] >= count)
            app->home_col[r] = count ? count - 1 : 0;
    }
    return &app->home;
}

static long findArtist(const Catalog *c, const char *name)
{
    for (size_t i = 0; i < c->artist_count; i++)
        if (strcmp(c->artists[i].name, name) == 0)
            return (long)i;
    return -1;
}

static long findAlbum(const Catalog *c, const char *title, const char *artist)
{
    for (size_t i = 0; i < c->album_count; i++)
        if (strcmp(c->albums[i].title, title) == 0 && strcmp(c->artists[c->albums[i].artist].name, artist) == 0)
            return (long)i;
    return -1;
}

void appLibrarySync(App *app, const Catalog *c)
{
    LibraryTab *t = &app->library;
    appLibraryPage(app);  // At least the first page.
    if (!c)
        return;
    bool same = c->generation == t->generation;
    for (int i = 0; i < t->depth; i++) {
        LibraryPage *p = &t->stack[i];
        if (p->level != LIB_ARTIST && p->level != LIB_ALBUM)
            continue;
        bool artist = p->level == LIB_ARTIST;
        if (!p->name[0] || same) {  // Remember what the index points at.
            if (artist && p->arg < c->artist_count) {
                snprintf(p->name, sizeof(p->name), "%s", c->artists[p->arg].name);
            } else if (!artist && p->arg < c->album_count) {
                snprintf(p->name, sizeof(p->name), "%s", c->albums[p->arg].title);
                snprintf(p->artist, sizeof(p->artist), "%s", c->artists[c->albums[p->arg].artist].name);
            }
            if (same || !p->name[0])
                continue;
        }
        long found = artist ? findArtist(c, p->name) : findAlbum(c, p->name, p->artist);
        if (found < 0) {  // Gone: back to the page before it.
            t->depth = i > 0 ? i : 1;
            break;
        }
        p->arg = (size_t)found;
    }
    t->generation = c->generation;
    LibraryPage *top = &t->stack[t->depth - 1];
    size_t count = appLibraryCount(app, c);
    if (top->cursor >= count)
        top->cursor = count ? count - 1 : 0;
}
