// The Home tab: rows of cards for artists, albums and playlists.
#include <stdio.h>
#include <string.h>

#include "catalog_scan.h"
#include "covercache.h"
#include "ui.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "ui_list.h"

#define CARD COVERCACHE_CARD
#define CARD_GAP 26
#define CARD_STEP (CARD + CARD_GAP)
#define SECTION_TOP 96
#define SECTION_H 226       // Title, cards and their two lines of text.
#define SECTION_TITLE_H 40
#define VISIBLE_SECTIONS 2
#define VISIBLE_CARDS ((GFX_WIDTH - 2 * MARGIN + CARD_GAP) / CARD_STEP)
#define FOCUS_RING 3

static const char *sectionTitle(const Home *home, HomeRowKind row)
{
    switch (row) {
    case HOME_TOP_ARTISTS: return "Your top artists";
    case HOME_RECENT_ALBUMS: return home->albums_from_library ? "Albums in your library" : "Recently played albums";
    case HOME_RECENT_ARTISTS: return "Recently played artists";
    default: return "Recent playlists";
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
        subtitle = "Playlist";
    } else if (row == HOME_RECENT_ALBUMS) {
        size_t album = home->rows[row].items[i];
        drawCover(c, album, x, y, false);
        title = c->albums[album].title;
        subtitle = c->artists[c->albums[album].artist].name;
    } else {
        const CatalogArtist *a = &c->artists[home->rows[row].items[i]];
        drawCover(c, a->albums[a->album_count - 1], x, y, true);  // Their latest album.
        title = a->name;
        snprintf(line, sizeof(line), "%zu song%s", a->song_count, a->song_count == 1 ? "" : "s");
        subtitle = line;
    }
    fontDrawClipped(selected ? FONT_BOLD : FONT_REGULAR, 17, x, y + CARD + 10, CARD + 8, title, COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 15, x, y + CARD + 34, CARD + 8, subtitle, COLOR_DIM);
}

static void drawEmpty(const Catalog *c)
{
    size_t done, total;
    bool scanning = catalogScanProgress(&done, &total);
    fontDraw(FONT_BOLD, 30, MARGIN, 150, "Welcome to KaiPlayer", COLOR_TEXT);
    const char *hint = scanning ? "Reading your music folder... your albums will show up here."
                     : c        ? "Your music folder has no songs yet. Add some over FTP."
                                : "Choose your music folder in + > Settings > Music folder.";
    fontDraw(FONT_REGULAR, 21, MARGIN, 200, hint, COLOR_DIM);
}

void uiDrawHome(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    (void)status;
    (void)now_ns;
    const Catalog *c = catalogLock();
    const Home *home = appHome((App *)app, c);
    int rows[HOME_ROW_COUNT], row_count = 0, focused = 0;
    for (int r = 0; r < HOME_ROW_COUNT; r++) {
        if (homeRowCount(home, (HomeRowKind)r) == 0 || (r != HOME_RECENT_PLAYLISTS && !c))
            continue;
        if (r == app->home_row)
            focused = row_count;
        rows[row_count++] = r;
    }
    if (row_count == 0) {
        drawEmpty(c);
        catalogUnlock();
        return;
    }
    // Keep the selected row on screen: it and the one after it.
    int first = focused > row_count - VISIBLE_SECTIONS ? row_count - VISIBLE_SECTIONS : focused;
    first = first < 0 ? 0 : first;
    bool list_focus = app->focus != FOCUS_PLAYER;
    for (int v = first; v < row_count && v < first + VISIBLE_SECTIONS; v++) {
        HomeRowKind row = (HomeRowKind)rows[v];
        int y = SECTION_TOP + (v - first) * SECTION_H;
        fontDraw(FONT_BOLD, 24, MARGIN, y, sectionTitle(home, row), COLOR_TEXT);
        size_t count = homeRowCount(home, row), col = app->home_col[row];
        size_t first_card = col >= (size_t)VISIBLE_CARDS ? col - VISIBLE_CARDS + 1 : 0;
        for (size_t i = first_card; i < count && i < first_card + VISIBLE_CARDS; i++) {
            int x = MARGIN + (int)(i - first_card) * CARD_STEP;
            bool selected = list_focus && (int)row == app->home_row && i == col;
            drawCard(home, c, row, i, x, y + SECTION_TITLE_H, selected);
            uiHitAdd(x, y + SECTION_TITLE_H, CARD, CARD + 54, HIT_CARD, (int)row * HOME_ROW_MAX + (int)i);
        }
    }
    catalogUnlock();
}
