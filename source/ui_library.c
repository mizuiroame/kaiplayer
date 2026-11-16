// The Library tab: the four sections, artists, albums and songs.
#include <stdio.h>
#include <string.h>

#include "catalog_scan.h"
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

// A round badge with the name's first letter, for artists.
static void drawArtistIcon(int x, int y, const char *name)
{
    char letter[8] = "?";
    const char *p = name;
    unsigned cp = utilNextCodePoint(&p);
    if (cp)
        snprintf(letter, sizeof(letter), "%.*s", (int)(p - name), name);
    gfxFillCircle(x + ICON_SIZE / 2, y + ICON_SIZE / 2, ICON_SIZE / 2, gfxMix(COLOR_SURFACE_HI, COLOR_ARTIST, 70));
    fontDrawCentered(FONT_BOLD, 15, x + ICON_SIZE / 2.0f, y + ICON_SIZE / 2.0f, letter, COLOR_ARTIST);
}

// A square sleeve with a record, for albums.
static void drawAlbumIcon(int x, int y)
{
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
    static const char *const NAMES[LIB_ROOT_ROWS] = {"Artists", "Albums", "Songs", "Folders"};
    size_t counts[LIB_ROOT_ROWS] = {c ? c->artist_count : 0, c ? c->album_count : 0, c ? c->song_count : 0, 0};
    for (size_t i = 0; i < LIB_ROOT_ROWS; i++) {
        int y = LIST_Y + (int)i * ROW_H;
        uiHitRow(y, i);
        if (i == cursor)
            uiDrawSelection(y, COLOR_ACCENT);
        int icon_y = y + (ROW_H - ICON_SIZE) / 2;
        if (i == 0)
            drawArtistIcon(ICON_X, icon_y, "A");
        else if (i == 1)
            drawAlbumIcon(ICON_X, icon_y);
        else if (i == 2)
            uiDrawNoteIcon(ICON_X + 3, icon_y + 2, 26, COLOR_ACCENT);
        else
            uiDrawFolderIcon(ICON_X + 2, icon_y + 5, COLOR_FOLDER);
        char detail[32] = "";
        if (i < 3 && c)
            snprintf(detail, sizeof(detail), "%zu", counts[i]);
        else if (i == 3)
            snprintf(detail, sizeof(detail), "%.28s", utilFileName(appMusicRoot(app)));
        drawRowText(y, i == cursor, NAMES[i], detail, COLOR_TEXT);
    }
}

static void drawArtistRow(const Catalog *c, size_t artist, int y, bool selected)
{
    const CatalogArtist *a = &c->artists[artist];
    drawArtistIcon(ICON_X, y + (ROW_H - ICON_SIZE) / 2, a->name);
    char detail[48];
    snprintf(detail, sizeof(detail), "%zu album%s  ·  %zu song%s", a->album_count, a->album_count == 1 ? "" : "s",
             a->song_count, a->song_count == 1 ? "" : "s");
    drawRowText(y, selected, a->name, detail, COLOR_TEXT);
}

static void drawAlbumRow(const Catalog *c, size_t album, int y, bool selected, bool with_artist)
{
    const CatalogAlbum *a = &c->albums[album];
    drawAlbumIcon(ICON_X, y + (ROW_H - ICON_SIZE) / 2);
    char detail[TAGS_TEXT_MAX + 32];
    if (with_artist)
        snprintf(detail, sizeof(detail), "%s", c->artists[a->artist].name);
    else if (a->year)
        snprintf(detail, sizeof(detail), "%d  ·  %zu song%s", a->year, a->song_count, a->song_count == 1 ? "" : "s");
    else
        snprintf(detail, sizeof(detail), "%zu song%s", a->song_count, a->song_count == 1 ? "" : "s");
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

// The line under the tabs: where we are, and what the scan is doing.
static void drawSubtitle(const App *app, const Catalog *c, const LibraryPage *p)
{
    int x = MARGIN;
    if (app->library.depth > 1)
        x += uiDrawBackButton(MARGIN - 4, SUBTITLE_Y - 6, app->focus == FOCUS_BACK) + 12;
    char line[TAGS_TEXT_MAX * 2 + 64] = "";
    switch (p->level) {
    case LIB_ROOT: snprintf(line, sizeof(line), "Library"); break;
    case LIB_ARTISTS: snprintf(line, sizeof(line), "Artists"); break;
    case LIB_ALBUMS: snprintf(line, sizeof(line), "Albums"); break;
    case LIB_SONGS: snprintf(line, sizeof(line), "Songs"); break;
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
    char scan[48] = "";
    if (catalogScanProgress(&done, &total))
        snprintf(scan, sizeof(scan), total ? "Updating library  %zu / %zu" : "Looking for music...", done, total);
    int scan_w = scan[0] ? fontTextWidth(FONT_REGULAR, TEXT_SMALL, scan) : 0;
    fontDrawClipped(FONT_BOLD, TEXT_SMALL, x, SUBTITLE_Y, GFX_WIDTH - MARGIN - x - scan_w - 24, line, COLOR_DIM);
    if (scan[0])
        fontDraw(FONT_REGULAR, TEXT_SMALL, GFX_WIDTH - MARGIN - scan_w, SUBTITLE_Y, scan, COLOR_ACCENT);
}

void uiDrawLibrary(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    const LibraryPage *p = appLibraryPage((App *)app);
    const Catalog *c = catalogLock();
    drawSubtitle(app, c, p);
    size_t count = appLibraryCount(app, c);
    if (p->level != LIB_ROOT && count == 0) {
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10,
                 c ? "Nothing here yet." : "Reading your music folder...", COLOR_DIM);
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
    size_t first = uiFirstVisible(p->cursor, count);
    for (size_t i = first; i < count && i < first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H;
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
    uiDrawScrollbar(first, count);
    catalogUnlock();
}
