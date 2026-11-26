// The Home tab: rows of cards for artists, albums and playlists.
#include <stdio.h>
#include <string.h>

#include "catalog_scan.h"
#include "covercache.h"
#include "lang.h"
#include "ui.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "ui_list.h"

#define CARD COVERCACHE_CARD
#define COLUMN_GAP 48
#define SECTION_W ((GFX_WIDTH - 2 * MARGIN - COLUMN_GAP) / 2)
#define CARD_STEP ((SECTION_W - CARD) / (HOME_ROW_MAX - 1))
#define SECTION_TOP 92
#define SECTION_H 236       // Title, cards and their two lines of text.
#define SECTION_TITLE_H 40
#define FOCUS_RING 3

static const char *sectionTitle(const Home *home, HomeRowKind row)
{
    switch (row) {
    case HOME_TOP_ARTISTS: return T("Your top artists");
    case HOME_RECENT_ALBUMS: return home->albums_from_library ? T("Albums in your library") : T("Recent albums");
    case HOME_RECENT_ARTISTS: return T("Recent artists");
    default: return T("Recent playlists");
    }
}

// The cover of an album (its first song's), drawn square or round.
static void drawCover(const Catalog *c, size_t album, int x, int y, bool round)
{
    const CoverImage *img = coverCacheGet(c->songs[c->albums[album].songs[0]].path, false);
    int radius = round ? CARD / 2 : 10;
    if (img) {
        gfxBlitRounded(x, y, CARD, img->size, img->pixels, radius);
        return;
    }
    gfxFillRoundRect(x, y, CARD, CARD, radius, COLOR_SURFACE_HI);
    uiDrawNoteIcon(x + CARD / 4, y + CARD / 4, CARD / 2, COLOR_FAINT);
}

static void drawPlaylistCover(int x, int y)
{
    gfxFillRoundRect(x, y, CARD, CARD, 10, gfxMix(COLOR_SURFACE_HI, COLOR_ACCENT, 120));
    uiDrawPlaylistAddIcon(x + CARD / 2.0f, y + CARD / 2.0f, 2.2f, COLOR_TEXT);
}

static void drawCard(const Home *home, const Catalog *c, HomeRowKind row, size_t i, int x, int y, bool selected)
{
    if (selected)
        gfxFillRoundRect(x - FOCUS_RING - 2, y - FOCUS_RING - 2, CARD + 2 * (FOCUS_RING + 2),
                         CARD + 2 * (FOCUS_RING + 2), row == HOME_RECENT_PLAYLISTS || row == HOME_RECENT_ALBUMS ? 14
                                                                                                             : CARD / 2 + 5,
                         COLOR_ACCENT);
    const char *title = "", *subtitle = "";
    char line[64];
    if (row == HOME_RECENT_PLAYLISTS) {
        drawPlaylistCover(x, y);
        title = home->playlists[i];
        subtitle = T("Playlist");
    } else if (row == HOME_RECENT_ALBUMS) {
        size_t album = home->rows[row].items[i];
        if (album >= c->album_count)
            return;
        drawCover(c, album, x, y, false);
        title = c->albums[album].title;
        subtitle = c->artists[c->albums[album].artist].name;
    } else {
        if (home->rows[row].items[i] >= c->artist_count)
            return;
        const CatalogArtist *a = &c->artists[home->rows[row].items[i]];
        drawCover(c, a->albums[a->album_count - 1], x, y, true);  // Their latest album.
        title = a->name;
        snprintf(line, sizeof(line), a->song_count == 1 ? T("%zu song") : T("%zu songs"), a->song_count);
        subtitle = line;
    }
    fontDrawClipped(selected ? FONT_BOLD : FONT_REGULAR, 17, x, y + CARD + 10, CARD + 4, title, COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 15, x, y + CARD + 34, CARD + 4, subtitle, COLOR_DIM);
}

static void drawEmpty(const Catalog *c)
{
    size_t done, total;
    bool scanning = catalogScanProgress(&done, &total);
    fontDraw(FONT_BOLD, 30, MARGIN, 150, T("Welcome to KaiPlayer"), COLOR_TEXT);
    const char *hint = scanning ? T("Reading your music folder... your albums will show up here.")
                     : c        ? T("Your music folder has no songs yet. Add some over FTP.")
                                : T("Choose your music folder in + > Settings > Music folder.");
    fontDraw(FONT_REGULAR, 21, MARGIN, 200, hint, COLOR_DIM);
}

// What an empty section says.
static const char *sectionEmpty(HomeRowKind row)
{
    return row == HOME_RECENT_PLAYLISTS ? T("Playlists you play show up here.") : T("Songs you play show up here.");
}

// The Internet Archive's Home: what it is, and a row that opens its pages in Library.
static void drawArchiveHome(const App *app)
{
    fontDraw(FONT_BOLD, 30, MARGIN, 150, "Internet Archive", COLOR_TEXT);
    fontDraw(FONT_REGULAR, 21, MARGIN, 200, T("Live concerts, 78 rpm records, public-domain classical music and free audio."),
             COLOR_DIM);
    int y = 250;
    uiHitRow(y, 0);
    if (app->focus != FOCUS_PLAYER)
        uiDrawSelection(y, COLOR_ARCHIVE);
    uiDrawSearchIcon(MARGIN + 7, y + 9, 26, COLOR_ARCHIVE);
    fontDraw(app->focus != FOCUS_PLAYER ? FONT_BOLD : FONT_REGULAR, 22, MARGIN + 52, y + 8,
             T("Browse the Internet Archive"), COLOR_TEXT);
}

void uiDrawHome(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    (void)status;
    (void)now_ns;
    if (appLibraryIsArchive(app)) {
        drawArchiveHome(app);
        return;
    }
    const Catalog *c = catalogLock();
    const Home *home = appHome((App *)app, c);
    bool any = false;
    for (int r = 0; r < HOME_ROW_COUNT; r++)
        any |= homeRowCount(home, (HomeRowKind)r) > 0;
    if (!any || !c) {
        drawEmpty(c);
        catalogUnlock();
        return;
    }
    bool list_focus = app->focus != FOCUS_PLAYER;
    for (int place = 0; place < HOME_ROW_COUNT; place++) {
        HomeRowKind row = HOME_GRID[place];
        int x0 = MARGIN + (place & 1) * (SECTION_W + COLUMN_GAP), y = SECTION_TOP + (place >> 1) * SECTION_H;
        fontDraw(FONT_BOLD, 22, x0, y, sectionTitle(home, row), COLOR_TEXT);
        size_t count = homeRowCount(home, row);
        if (count == 0) {
            fontDraw(FONT_REGULAR, 18, x0, y + SECTION_TITLE_H + 10, sectionEmpty(row), COLOR_FAINT);
            continue;
        }
        for (size_t i = 0; i < count; i++) {
            int x = x0 + (int)i * CARD_STEP;
            bool selected = list_focus && (int)row == app->home_row && i == app->home_col[row];
            drawCard(home, c, row, i, x, y + SECTION_TITLE_H, selected);
            uiHitAdd(x, y + SECTION_TITLE_H, CARD, CARD + 54, HIT_CARD, (int)row * HOME_ROW_MAX + (int)i);
        }
    }
    catalogUnlock();
}
