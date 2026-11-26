// The Library tab: the four sections, artists, albums and songs.
#include <stdio.h>
#include <string.h>

#include "catalog_scan.h"
#include "covercache.h"
#include "lang.h"
#include "ui.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "ui_list.h"

#define ICON_X (MARGIN + 4)
#define NAME_X (MARGIN + 52)
#define ICON_SIZE 30
#define COLUMN_X 760
#define COLOR_ARTIST GFX_RGB(0xC3, 0x9B, 0xFF)
// The Internet Archive's rows: a quiet color of their own.
#define COLOR_ARCHIVE GFX_RGB(0xE8, 0xC1, 0x6B)
#define HINT_LINE_H 28

// An artist: their latest album's cover in a circle, or a person icon.
static void drawArtistIcon(int x, int y, const char *song_path)
{
    const CoverImage *cover = song_path ? coverCacheGet(song_path, true) : NULL;
    if (cover) {
        gfxBlitRounded(x, y, ICON_SIZE, cover->size, cover->pixels, ICON_SIZE / 2);
        return;
    }
    gfxFillCircle(x + ICON_SIZE / 2, y + ICON_SIZE / 2, ICON_SIZE / 2, gfxMix(COLOR_SURFACE_HI, COLOR_ARTIST, 70));
    uiDrawPersonIcon(x + ICON_SIZE / 2.0f, y + ICON_SIZE / 2.0f, ICON_SIZE * 0.9f, COLOR_ARTIST);
}

// The album's cover, or a square sleeve with a record while it loads.
static void drawAlbumIcon(int x, int y, const char *song_path)
{
    const CoverImage *cover = song_path ? coverCacheGet(song_path, true) : NULL;
    if (cover) {
        gfxBlitRounded(x, y, ICON_SIZE, cover->size, cover->pixels, 4);
        return;
    }
    gfxFillRoundRect(x, y, ICON_SIZE, ICON_SIZE, 5, COLOR_SURFACE_HI);
    gfxFillCircle(x + ICON_SIZE / 2, y + ICON_SIZE / 2, 10, COLOR_ACCENT);
    gfxFillCircle(x + ICON_SIZE / 2, y + ICON_SIZE / 2, 3, COLOR_SURFACE_HI);
}

static void drawRowText(int y, bool selected, const char *name, const char *detail, Color color)
{
    int name_w = (detail && detail[0] ? COLUMN_X - 24 : GFX_WIDTH - MARGIN - 80) - NAME_X;
    fontDrawClipped(selected ? FONT_BOLD : FONT_REGULAR, TEXT_ROW, NAME_X, y + 8, name_w, name, color);
    if (detail && detail[0])
        fontDrawClipped(FONT_REGULAR, 20, COLUMN_X, y + 10, GFX_WIDTH - MARGIN - 80 - COLUMN_X, detail, COLOR_DIM);
}

static void drawRoot(const App *app, const Catalog *c, size_t cursor)
{
    static const char *const NAMES[LIB_ROOT_ROWS] = {N_("Artists"), N_("Albums"), N_("Songs"), N_("Folders"),
                                                     "Internet Archive"};
    size_t counts[LIB_ROOT_ROWS] = {c ? c->artist_count : 0, c ? c->album_count : 0, c ? c->song_count : 0, 0, 0};
    for (size_t i = 0; i < LIB_ROOT_ROWS; i++) {
        int y = LIST_Y + (int)i * ROW_H;
        uiHitRow(y, i);
        if (i == cursor)
            uiDrawSelection(y, COLOR_ACCENT);
        int icon_y = y + (ROW_H - ICON_SIZE) / 2;
        if (i == 0)
            drawArtistIcon(ICON_X, icon_y, NULL);
        else if (i == 1)
            drawAlbumIcon(ICON_X, icon_y, NULL);
        else if (i == 2)
            uiDrawNoteIcon(ICON_X + 3, icon_y + 2, 26, COLOR_ACCENT);
        else if (i == LIB_ROOT_ARCHIVE)
            uiDrawNoteIcon(ICON_X + 3, icon_y + 2, 26, COLOR_ARCHIVE);
        else
            uiDrawFolderIcon(ICON_X + 2, icon_y + 5, COLOR_FOLDER);
        char detail[64] = "";
        if (i == LIB_ROOT_ARCHIVE) {  // A name: not translated.
            snprintf(detail, sizeof(detail), "%s", T("Concerts, 78s, classical: online"));
            drawRowText(y, i == cursor, NAMES[i], detail, COLOR_TEXT);
            continue;
        }
        if (i < 3 && c)
            snprintf(detail, sizeof(detail), "%zu", counts[i]);
        else if (i == 3 && appLibrarySource(app) == LIBRARY_NETWORK) {
            char address[64], user[8];
            appNetworkShown(address, sizeof(address), user, sizeof(user));
            snprintf(detail, sizeof(detail), "%.40s", address);
        } else if (i == 3 && appLibrarySource(app) == LIBRARY_SERVER) {
            char address[64], user[8];
            bool check;
            appServerShown(address, sizeof(address), user, sizeof(user), &check);
            snprintf(detail, sizeof(detail), "%.40s", address);
        } else if (i == 3)
            snprintf(detail, sizeof(detail), "%.28s", utilFileName(appMusicRoot(app)));
        drawRowText(y, i == cursor, T(NAMES[i]), detail, COLOR_TEXT);
    }
}

static void drawArtistRow(const Catalog *c, size_t artist, int y, bool selected)
{
    const CatalogArtist *a = &c->artists[artist];
    const CatalogAlbum *latest = &c->albums[a->albums[a->album_count - 1]];
    drawArtistIcon(ICON_X, y + (ROW_H - ICON_SIZE) / 2, c->songs[latest->songs[0]].path);
    char albums[64], songs[64], detail[160];
    snprintf(albums, sizeof(albums), a->album_count == 1 ? T("%zu album") : T("%zu albums"), a->album_count);
    snprintf(songs, sizeof(songs), a->song_count == 1 ? T("%zu song") : T("%zu songs"), a->song_count);
    snprintf(detail, sizeof(detail), "%s  ·  %s", albums, songs);
    drawRowText(y, selected, a->name, detail, COLOR_TEXT);
}

static void drawAlbumRow(const Catalog *c, size_t album, int y, bool selected, bool with_artist)
{
    const CatalogAlbum *a = &c->albums[album];
    drawAlbumIcon(ICON_X, y + (ROW_H - ICON_SIZE) / 2, c->songs[a->songs[0]].path);
    char detail[TAGS_TEXT_MAX + 32], songs[64];
    snprintf(songs, sizeof(songs), a->song_count == 1 ? T("%zu song") : T("%zu songs"), a->song_count);
    if (with_artist)
        snprintf(detail, sizeof(detail), "%s", c->artists[a->artist].name);
    else if (a->year)
        snprintf(detail, sizeof(detail), "%d  ·  %s", a->year, songs);
    else
        snprintf(detail, sizeof(detail), "%s", songs);
    drawRowText(y, selected, a->title, detail, COLOR_TEXT);
}

static void drawSongRow(const App *app, const Catalog *c, size_t song, size_t row, int y, bool selected,
                        const char *playing, const PlayerStatus *status, uint64_t now_ns)
{
    const CatalogSong *s = &c->songs[song];
    TrackRow r = {
        .number = s->track > 0 && appLibraryPage((App *)app)->level == LIB_ALBUM ? (size_t)s->track : row + 1,
        .path = s->path, .y = y, .index = row, .selected = selected,
        .current = playing[0] && strcmp(playing, s->path) == 0,
        .playing = status->state == PLAYER_PLAYING, .now_ns = now_ns,
    };
    uiDrawTrackRow(&r);
}

// Words wrapped over at most lines lines of w, from y.
static void drawWrapped(int x, int y, int w, const char *text, Color color, int lines)
{
    char line[512];
    const char *p = text;
    for (int drawn = 0; *p && drawn < lines; drawn++) {
        size_t fit = 0;  // Bytes up to a space (or the end) that fit.
        for (size_t n = 1; p[n - 1] && n < sizeof(line); n++) {
            if (p[n] != ' ' && p[n] != '\0')
                continue;
            memcpy(line, p, n);
            line[n] = '\0';
            if (fit && fontTextWidth(FONT_REGULAR, TEXT_ROW, line) > w)
                break;
            fit = n;
            if (!p[n])
                break;
        }
        if (!fit)
            fit = strlen(p) < sizeof(line) - 1 ? strlen(p) : sizeof(line) - 1;
        memcpy(line, p, fit);
        line[fit] = '\0';
        fontDrawClipped(FONT_REGULAR, TEXT_ROW, x, y + drawn * HINT_LINE_H, w, line, color);
        p += fit;
        while (*p == ' ')
            p++;
    }
}

static void drawStreamRow(const App *app, const StreamPage *s, size_t i, int y, bool selected, const char *playing,
                          const PlayerStatus *status, uint64_t now_ns)
{
    int icon_y = y + (ROW_H - ICON_SIZE) / 2;
    if (s->kind == STREAM_PAGE_MENU) {
        if (streamMenuSearches((int)i))
            uiDrawSearchIcon(ICON_X + 3, icon_y + 2, 26, COLOR_ARCHIVE);
        else
            uiDrawNoteIcon(ICON_X + 3, icon_y + 2, 26, COLOR_ARCHIVE);
        drawRowText(y, selected, streamMenuLabel((int)i), "", COLOR_TEXT);
        return;
    }
    if (s->failed && s->list.count == 0) {
        drawRowText(y, selected, T("Try again"), "", COLOR_ACCENT);
        return;
    }
    const StreamItem *item = &s->list.items[i];
    char detail[STREAM_TEXT_MAX + STREAM_DETAIL_MAX + 8];
    switch (item->kind) {
    case STREAM_ITEM_TRACK: {
        TrackRow r = {
            .number = i + 1,
            .path = item->location, .y = y, .index = i, .selected = selected,
            .current = playing[0] && strcmp(playing, item->location) == 0,
            .playing = status->state == PLAYER_PLAYING, .now_ns = now_ns,
        };
        uiDrawTrackRow(&r);
        return;
    }
    default:
        drawAlbumIcon(ICON_X, icon_y, item->location[0] ? item->location : NULL);
        snprintf(detail, sizeof(detail), "%s%s%s", item->artist,
                 item->artist[0] && item->detail[0] ? "  \xC2\xB7  " : "", item->detail);
        drawRowText(y, selected, item->title, detail, COLOR_TEXT);
        return;
    }
}

// Below an Archive page's rows: why it has nothing (it failed), or what it is.
static void drawStreamNote(const StreamPage *s, size_t count)
{
    if (count > UI_LIST_ROWS - 3)
        return;  // A full list: no room.
    int below = LIST_Y + (int)count * ROW_H + 18;
    int w = GFX_WIDTH - 2 * MARGIN - 40;
    if (s->failed && s->list.count == 0 && s->why[0])
        drawWrapped(MARGIN, below, w, s->why, COLOR_ERROR, 3);
    else if (s->kind == STREAM_PAGE_MENU)
        drawWrapped(MARGIN, below, w,
                    T("Played as it streams from archive.org, nothing downloaded: the Live Music Archive's concerts "
                      "(from bands that allow it, non-commercial), old 78 rpm records, public-domain classical music, "
                      "and audio with a Creative Commons license. Each song's license shows in Now Playing."),
                    COLOR_DIM, 4);
}

// An online page: its rows, or what it's waiting for, or why it has none.
static void drawStream(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    const StreamPage *s = appStreamPage(app);
    const LibraryPage *p = appLibraryPage((App *)app);
    size_t count = appStreamCount(app);
    if (!s)
        return;
    if (s->kind == STREAM_PAGE_RESULTS && s->loading) {
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10, T("Asking..."), COLOR_DIM);
        return;
    }
    if (count == 0) {
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10, T("Nothing found."), COLOR_DIM);
        return;
    }
    char playing[UTIL_PATH_MAX] = "";
    if (status->state != PLAYER_STOPPED)
        playerTrackPath(status->track, playing, sizeof(playing));
    bool songs = appStreamPageHasSongs(s);
    uiHitListArea();
    int offset;
    size_t first = uiListBegin(&p->cursor, p->cursor, count, &offset);
    for (size_t i = first; i < count && i <= first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H - offset;
        bool selected = i == p->cursor;
        uiHitRow(y, i);
        if (selected && !songs)
            uiDrawSelection(y, COLOR_ACCENT);
        drawStreamRow(app, s, i, y, selected, playing, status, now_ns);
    }
    uiListEnd();
    uiDrawScrollbar(first, count);
    drawStreamNote(s, count);
}

// The line under the tabs: where we are, and what the scan is doing.
static void drawSubtitle(const App *app, const Catalog *c, const LibraryPage *p)
{
    int x = MARGIN;
    if (app->library.depth > 1)
        x += uiDrawBackButton(MARGIN - 4, SUBTITLE_Y - 6, app->focus == FOCUS_BACK) + 12;
    char line[TAGS_TEXT_MAX * 2 + 64] = "";
    switch (p->level) {
    case LIB_ROOT: snprintf(line, sizeof(line), "%s", T("Library")); break;
    case LIB_ARTISTS: snprintf(line, sizeof(line), "%s", T("Artists")); break;
    case LIB_ALBUMS: snprintf(line, sizeof(line), "%s", T("Albums")); break;
    case LIB_SONGS: snprintf(line, sizeof(line), "%s", T("Songs")); break;
    case LIB_STREAM:
        if (appStreamPage(app))
            snprintf(line, sizeof(line), "%s", appStreamPage(app)->title);
        break;
    case LIB_ARTIST:
        if (c && p->arg < c->artist_count)
            snprintf(line, sizeof(line), "%s", c->artists[p->arg].name);
        break;
    case LIB_ALBUM:
        if (c && p->arg < c->album_count) {
            const CatalogAlbum *a = &c->albums[p->arg];
            snprintf(line, sizeof(line), "%s  ·  %s", a->title, c->artists[a->artist].name);
        }
        break;
    default: break;
    }
    size_t done, total;
    char scan[192] = "";
    bool problem = false;
    if (catalogScanProgress(&done, &total)) {
        if (total)
            snprintf(scan, sizeof(scan), T("Updating library  %zu / %zu"), done, total);
        else
            snprintf(scan, sizeof(scan), "%s", T("Looking for music..."));
    } else if (p->level == LIB_STREAM) {
        // An online page: the library's own problems aren't about it.
    } else if (appLibraryIsRemote(app)) {  // Why the network folder's or the server's couldn't be looked at.
        catalogScanProblem(appMusicRoot(app), scan, sizeof(scan));
        problem = scan[0] != '\0';
    }
    int scan_w = scan[0] ? fontTextWidth(FONT_REGULAR, TEXT_SMALL, scan) : 0;
    scan_w = scan_w > GFX_WIDTH / 2 ? GFX_WIDTH / 2 : scan_w;
    fontDrawClipped(FONT_BOLD, TEXT_SMALL, x, SUBTITLE_Y, GFX_WIDTH - MARGIN - x - scan_w - 24, line, COLOR_DIM);
    if (scan[0])
        fontDrawClipped(FONT_REGULAR, TEXT_SMALL, GFX_WIDTH - MARGIN - scan_w, SUBTITLE_Y, scan_w, scan,
                        problem ? COLOR_ERROR : COLOR_ACCENT);
}

void uiDrawLibrary(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    const Catalog *c = catalogLock();
    appLibrarySync((App *)app, c);
    const LibraryPage *p = appLibraryPage((App *)app);
    drawSubtitle(app, c, p);
    size_t count = appLibraryCount(app, c);
    if (p->level == LIB_STREAM) {
        drawStream(app, status, now_ns);
        catalogUnlock();
        return;
    }
    if (p->level != LIB_ROOT && count == 0) {
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10,
                 c ? T("Nothing here yet.") : T("Reading your music folder..."), COLOR_DIM);
        catalogUnlock();
        return;
    }
    uiHitListArea();
    if (p->level == LIB_ROOT) {
        drawRoot(app, c, p->cursor);
        catalogUnlock();
        return;
    }
    char playing[UTIL_PATH_MAX] = "";
    if (status->state != PLAYER_STOPPED)
        playerTrackPath(status->track, playing, sizeof(playing));
    int offset;
    size_t first = uiListBegin(&p->cursor, p->cursor, count, &offset);
    for (size_t i = first; i < count && i <= first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H - offset;
        bool selected = i == p->cursor;
        uiHitRow(y, i);
        if (selected && p->level != LIB_ALBUM && p->level != LIB_SONGS)
            uiDrawSelection(y, COLOR_ACCENT);
        switch (p->level) {
        case LIB_ARTISTS: drawArtistRow(c, i, y, selected); break;
        case LIB_ARTIST: drawAlbumRow(c, c->artists[p->arg].albums[i], y, selected, false); break;
        case LIB_ALBUMS: drawAlbumRow(c, i, y, selected, true); break;
        case LIB_ALBUM: drawSongRow(app, c, c->albums[p->arg].songs[i], i, y, selected, playing, status, now_ns); break;
        case LIB_SONGS: drawSongRow(app, c, c->songs_by_title[i], i, y, selected, playing, status, now_ns); break;
        default: break;
        }
        if (p->level != LIB_ALBUM && p->level != LIB_SONGS)
            uiDrawAddButton(GFX_WIDTH - MARGIN - 16 - ADD_BUTTON_W + 8, y, i, false);
    }
    uiListEnd();
    uiDrawScrollbar(first, count);
    catalogUnlock();
}
