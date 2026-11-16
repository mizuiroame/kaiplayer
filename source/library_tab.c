// The Library tab's page stack and the Home rows, shared by input and drawing.
#include "app.h"

LibraryPage *appLibraryPage(App *app)
{
    LibraryTab *t = &app->library;
    if (t->depth == 0)
        t->stack[t->depth++] = (LibraryPage){LIB_ROOT, 0, 0};
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

// Recomputes the rows when the catalog or the history changed.
const Home *appHome(App *app, const Catalog *c)
{
    size_t songs = c ? c->song_count : 0;
    if (c != app->home_catalog || songs != app->home_songs || app->history.count != app->home_history ||
        !app->home_ready) {
        homeCompute(&app->home, c, &app->history);
        app->home_catalog = c;
        app->home_songs = songs;
        app->home_history = app->history.count;
        app->home_ready = true;
    }
    return &app->home;
}
