// The Library tab: Artists, Albums, Songs and Folders, with a stack of pages
// so B goes back the way you came.
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_buttons.h"
#include "catalog_scan.h"
#include "lang.h"
#include "ui.h"

#define PAGE_ROWS UI_LIST_ROWS

static void push(App *app, LibraryLevel level, size_t arg)
{
    LibraryTab *t = &app->library;
    if (t->depth < LIBRARY_DEPTH_MAX)
        t->stack[t->depth++] = (LibraryPage){.level = level, .arg = arg};  // Its name is filled in by appLibrarySync.
    app->focus = FOCUS_LIST;
}

void appLibraryBack(App *app)
{
    if (app->library.depth > 1)
        app->library.depth--;
    app->focus = FOCUS_LIST;
}

// The songs of the selected row (or of the whole page when the row is the
// page itself), in play order. start: the selected song's place in them.
bool appLibrarySongs(App *app, const Catalog *c, bool whole_page, TrackList *out, size_t *start)
{
    *out = (TrackList){0};
    *start = 0;
    const LibraryPage *p = appLibraryPage(app);
    const CatalogArtist *artist = NULL;
    const CatalogAlbum *album = NULL;
    switch (p->level) {
    case LIB_ARTISTS:
        if (p->cursor < c->artist_count)
            artist = &c->artists[p->cursor];
        break;
    case LIB_ARTIST:
        if (p->arg >= c->artist_count)
            return false;
        if (whole_page)
            artist = &c->artists[p->arg];
        else if (p->cursor < c->artists[p->arg].album_count)
            album = &c->albums[c->artists[p->arg].albums[p->cursor]];
        break;
    case LIB_ALBUMS:
        if (p->cursor < c->album_count)
            album = &c->albums[p->cursor];
        break;
    case LIB_ALBUM:
        if (p->arg >= c->album_count)
            return false;
        album = &c->albums[p->arg];
        *start = whole_page ? 0 : p->cursor;
        break;
    case LIB_SONGS:
        for (size_t i = 0; i < c->song_count; i++)
            if (!trackListAdd(out, c->songs[c->songs_by_title[i]].path))
                return false;
        *start = whole_page ? 0 : p->cursor;
        return true;
    default:
        return false;
    }
    if (artist) {
        for (size_t a = 0; a < artist->album_count; a++) {
            const CatalogAlbum *al = &c->albums[artist->albums[a]];
            for (size_t k = 0; k < al->song_count; k++)
                if (!trackListAdd(out, c->songs[al->songs[k]].path))
                    return false;
        }
    } else if (album) {
        for (size_t k = 0; k < album->song_count; k++)
            if (!trackListAdd(out, c->songs[album->songs[k]].path))
                return false;
    }
    return out->count > 0;
}

// Plays the selection: a song plays its album (or list) from there; an
// artist or album plays all of it.
static void play(App *app, bool whole_page, bool shuffle)
{
    const Catalog *c = catalogLock();
    TrackList list = {0};
    size_t start = 0;
    bool ok = c && appLibrarySongs(app, c, whole_page, &list, &start);
    const LibraryPage *p = appLibraryPage(app);
    char source[PLAYLIST_NAME_MAX + 1] = "";
    bool mixed = false;  // An artist or "Songs" mixes albums.
    if (ok && (p->level == LIB_ARTISTS || (p->level == LIB_ARTIST && whole_page))) {
        size_t a = p->level == LIB_ARTISTS ? p->cursor : p->arg;
        snprintf(source, sizeof(source), "%.48s", c->artists[a].name);
        mixed = true;
    } else if (ok && p->level == LIB_SONGS) {
        snprintf(source, sizeof(source), "%s", T("All songs"));
        mixed = true;
    }
    catalogUnlock();
    if (!ok) {
        trackListFree(&list);
        appToast(app, T("There are no songs to play."));
        return;
    }
    appPlayTracks(app, &list, start, shuffle, mixed ? source : NULL);
    trackListFree(&list);
}

void appLibraryPlayPage(App *app, bool shuffle)
{
    play(app, true, shuffle);
}

static void openRow(App *app)
{
    LibraryPage *p = appLibraryPage(app);
    const Catalog *c = catalogLock();
    size_t count = appLibraryCount(app, c);
    size_t cursor = p->cursor;
    LibraryLevel level = p->level;
    size_t album = 0;
    if (c && level == LIB_ARTIST && p->arg < c->artist_count && cursor < count)
        album = c->artists[p->arg].albums[cursor];
    catalogUnlock();
    if (level == LIB_ROOT) {
        static const LibraryLevel ROWS[LIB_ROOT_ROWS] = {LIB_ARTISTS, LIB_ALBUMS, LIB_SONGS, LIB_FOLDERS};
        if (cursor < LIB_ROOT_ROWS && ROWS[cursor] == LIB_FOLDERS) {
            if (appLibrarySource(app) == LIBRARY_NETWORK)
                appNetworkEnsurePassword(app);  // Without it, Folders says why.
            appOpenFolders(app);
        }  // Listed now (a network folder's waits for its server).
        if (cursor < LIB_ROOT_ROWS)
            push(app, ROWS[cursor], 0);
        return;
    }
    if (cursor >= count)
        return;
    switch (level) {
    case LIB_ARTISTS: push(app, LIB_ARTIST, cursor); break;
    case LIB_ARTIST: push(app, LIB_ALBUM, album); break;
    case LIB_ALBUMS: push(app, LIB_ALBUM, cursor); break;
    case LIB_ALBUM:
    case LIB_SONGS: play(app, false, false); break;
    default: break;
    }
}

void appHandleLibrary(App *app, uint64_t pressed)
{
    LibraryPage *p = appLibraryPage(app);
    if (p->level == LIB_FOLDERS) {
        appHandleBrowser(app, pressed);
        return;
    }
    const Catalog *c = catalogLock();
    appLibrarySync(app, c);
    p = appLibraryPage(app);
    size_t count = appLibraryCount(app, c);
    catalogUnlock();
    bool has_back = app->library.depth > 1;
    bool has_add = p->level != LIB_ROOT && count > 0;
    pressed = appFocusFilter(app, pressed, p->cursor, count, has_back, has_add);

    long cursor = (long)p->cursor;
    if (pressed & BUTTONS_UP) cursor--;
    if (pressed & BUTTONS_DOWN) cursor++;
    if (pressed & BUTTONS_PAGE_UP) cursor -= PAGE_ROWS;
    if (pressed & BUTTONS_PAGE_DOWN) cursor += PAGE_ROWS;
    cursor = cursor >= (long)count ? (long)count - 1 : cursor;
    p->cursor = cursor < 0 ? 0 : (size_t)cursor;

    if (pressed & BUTTON_ROW_ACTION)
        appAddToPlaylist(app, SRC_LIBRARY_ENTRY);
    if (pressed & HidNpadButton_A) {
        if (app->focus == FOCUS_BACK)
            appLibraryBack(app);
        else
            openRow(app);
    }
    if (pressed & HidNpadButton_B)
        appLibraryBack(app);
    // X plays and Y shuffles the page (an album, an artist, all songs) or the
    // selected artist or album in a list of them.
    bool page = p->level == LIB_ARTIST || p->level == LIB_ALBUM || p->level == LIB_SONGS;
    if ((pressed & HidNpadButton_X) && p->level != LIB_ROOT)
        play(app, page, false);
    else if ((pressed & HidNpadButton_X) && appCanSwitchSource(app))
        appSwitchLibrarySource(app);  // The card's music, the network folder's, or the server's.
    if ((pressed & HidNpadButton_Y) && p->level != LIB_ROOT)
        play(app, page, true);
}
