#include "ui_list.h"

#include <math.h>
#include <stdio.h>

#include "ui.h"
#include "ui_common.h"
#include "meta.h"
#include "ui_hit.h"

#define COLUMN_END (GFX_WIDTH - MARGIN - 16 - ADD_BUTTON_W - 12)
#define COLUMN_GAP 24
#define TEXT_COLUMN 20
// Brighter than COLOR_DIM: small grey text on the dark rows looked soft.
#define COLOR_COLUMN GFX_RGB(0xCC, 0xCC, 0xD6)
#define BAR_W 4
#define BAR_STEP 7
#define BAR_MAX_H 20

size_t uiFirstVisible(size_t cursor, size_t count)
{
    if (count <= UI_LIST_ROWS || cursor < UI_LIST_ROWS / 2)
        return 0;
    size_t first = cursor - UI_LIST_ROWS / 2;
    return first + UI_LIST_ROWS > count ? count - UI_LIST_ROWS : first;
}

bool g_ui_list_focused = true;
UiListScroll g_list_scroll;

static size_t clampFirst(size_t first, size_t count)
{
    return count <= UI_LIST_ROWS ? 0 : first > count - UI_LIST_ROWS ? count - UI_LIST_ROWS : first;
}

size_t uiListBegin(const void *key, size_t cursor, size_t count, int *offset)
{
    UiListScroll *s = &g_list_scroll;
    if (s->key != key)
        s->free = false;
    s->key = key;
    s->cursor = cursor;
    s->count = count;
    size_t first = s->free ? clampFirst(s->first, count) : uiFirstVisible(cursor, count);
    *offset = s->free && first == s->first ? s->offset : 0;
    gfxSetClip(0, LIST_Y, GFX_WIDTH, UI_LIST_ROWS * ROW_H);
    uiHitClip(LIST_Y, LIST_Y + UI_LIST_ROWS * ROW_H);
    return first;
}

void uiListEnd(void)
{
    gfxResetClip();
    uiHitClip(-100000, 100000);
}

bool uiListScrollPx(int px)
{
    UiListScroll *s = &g_list_scroll;
    if (!s->free) {
        s->first = uiFirstVisible(s->cursor, s->count);
        s->offset = 0;
        s->free = true;
    }
    long end = s->count > UI_LIST_ROWS ? (long)(s->count - UI_LIST_ROWS) * ROW_H : 0;
    long at = (long)s->first * ROW_H + s->offset, to = at + px;
    to = to < 0 ? 0 : to > end ? end : to;
    s->first = (size_t)(to / ROW_H);
    s->offset = (int)(to % ROW_H);
    s->want_cursor = s->cursor;
    if (s->count > 0) {  // The cursor stays on a row mostly in view.
        size_t top = s->first + (s->offset * 2 > ROW_H ? 1 : 0);
        size_t bottom = top + UI_LIST_ROWS - 1 < s->count ? top + UI_LIST_ROWS - 1 : s->count - 1;
        if (s->cursor < top)
            s->want_cursor = top;
        else if (s->cursor > bottom)
            s->want_cursor = bottom;
    }
    return to != at && to == at + px;
}

void uiListRelease(void)
{
    g_list_scroll.free = false;
}

void uiDrawSelection(int y, Color bar)
{
    if (!g_ui_list_focused)
        return;
    gfxFillRoundRect(MARGIN - 12, y + 2, GFX_WIDTH - 2 * (MARGIN - 12), ROW_H - 4, 8, COLOR_SURFACE_HI);
    gfxFillRect(MARGIN - 12, y + 10, 3, ROW_H - 20, bar);
}

void uiDrawFolderIcon(int x, int y, Color color)
{
    gfxFillRoundRect(x, y, 10, 6, 2, color);
    gfxFillRoundRect(x, y + 3, 22, 15, 3, color);
}

void uiDrawScrollbar(size_t first, size_t count)
{
    if (count <= UI_LIST_ROWS)
        return;
    int track_h = UI_LIST_ROWS * ROW_H;
    int thumb_h = track_h * UI_LIST_ROWS / (int)count;
    if (thumb_h < 24)
        thumb_h = 24;
    long at = (long)first * ROW_H + (g_list_scroll.free ? g_list_scroll.offset : 0);
    long end = (long)(count - UI_LIST_ROWS) * ROW_H;
    int thumb_y = LIST_Y + (int)((long)(track_h - thumb_h) * (at < end ? at : end) / end);
    gfxFillRoundRect(GFX_WIDTH - 12, LIST_Y, 4, track_h, 2, COLOR_SURFACE);
    gfxFillRoundRect(GFX_WIDTH - 12, thumb_y, 4, thumb_h, 2, COLOR_FAINT);
}

void uiDrawAddButton(int x, int y, size_t index, bool focused)
{
    int cx = x + ADD_BUTTON_W / 2, cy = y + ROW_H / 2;
    Color mark = focused ? COLOR_BG : COLOR_DIM;
    gfxFillCircle(cx, cy, focused ? 16 : 14, focused ? COLOR_ACCENT : COLOR_SURFACE_HI);
    uiDrawPlaylistAddIcon((float)cx, (float)cy, 0.75f, mark);
    uiHitAdd(x, y, ADD_BUTTON_W, ROW_H, HIT_ADD, (int)index);
}

void uiDrawRemoveButton(int x, int y, size_t index, bool focused)
{
    int cx = x + ADD_BUTTON_W / 2, cy = y + ROW_H / 2;
    Color mark = focused ? COLOR_BG : COLOR_DIM;
    gfxFillCircle(cx, cy, focused ? 16 : 14, focused ? COLOR_ERROR : COLOR_SURFACE_HI);
    uiDrawPlaylistRemoveIcon((float)cx, (float)cy, 0.75f, mark);
    uiHitAdd(x, y, ADD_BUTTON_W, ROW_H, HIT_ADD, (int)index);  // The tab decides what the button does.
}

int uiDrawBackButton(int x, int y, bool focused)
{
    return uiDrawBackButtonHit(x, y, focused, HIT_BACK);
}

int uiDrawBackButtonHit(int x, int y, bool focused, HitKind hit)
{
    static const char LABEL[] = "Back";
    int w = fontTextWidth(FONT_BOLD, 17, LABEL) + 44;
    Color text = focused ? COLOR_BG : COLOR_TEXT;
    float mid = (float)y + 15.0f;
    gfxFillRoundRect(x, y, w, 30, 15, focused ? COLOR_ACCENT : COLOR_SURFACE_HI);
    gfxTriangle((float)x + 12.0f, mid, (float)x + 21.0f, mid - 6.5f, (float)x + 21.0f, mid + 6.5f, text);
    fontDrawMiddle(FONT_BOLD, 17, x + 28, mid, LABEL, text);
    uiHitAdd(x - 8, y - 10, w + 16, 50, hit, 0);
    return w;
}

void uiDrawPlayingBars(int x, int y, bool playing, uint64_t now_ns)
{
    static const double SPEED[3] = {7.0, 9.5, 6.0}, PHASE[3] = {0.0, 1.7, 3.1};
    static const int STILL[3] = {8, 14, 10};
    double t = (double)(now_ns % 1000000000000ull) / 1e9;
    for (int i = 0; i < 3; i++) {
        int h = playing ? 4 + (int)((BAR_MAX_H - 4) * (0.5 + 0.5 * sin(t * SPEED[i] + PHASE[i]))) : STILL[i];
        gfxFillRoundRect(x + i * BAR_STEP, y + BAR_MAX_H - h, BAR_W, h, 1, COLOR_ACCENT);
    }
}

void uiDrawColumnHeaders(void)
{
    fontDraw(FONT_BOLD, 15, COLUMN_ARTIST_X, SUBTITLE_Y + 2, "ARTIST", COLOR_FAINT);
    fontDraw(FONT_BOLD, 15, COLUMN_ALBUM_X, SUBTITLE_Y + 2, "ALBUM", COLOR_FAINT);
}

void uiLoadSong(const char *path, UiSong *song)
{
    uiSplitTrackPath(path, song->title, song->folder);
    song->loaded = metaLookup(path, &song->meta);
    if (song->loaded && song->meta.title[0])
        snprintf(song->title, sizeof(song->title), "%s", song->meta.title);
}

#define PLACEHOLDER_W 220

void uiDrawSongTitle(const UiSong *song, FontFace face, int x, int y, int max_width, Color color)
{
    if (song->loaded) {
        fontDrawClipped(face, TEXT_ROW, x, y, max_width, song->title, color);
        return;
    }
    int w = fontTextWidth(face, TEXT_ROW, song->title);
    w = w < PLACEHOLDER_W ? w : PLACEHOLDER_W;
    // Light enough to show on the selected row too.
    gfxFillRoundRect(x, y + 9, w < max_width ? w : max_width, 14, 7, gfxMix(COLOR_SURFACE_HI, COLOR_FAINT, 110));
}

void uiDrawSongColumns(const UiSong *song, int y)
{
    if (!song->loaded)
        return;  // Still being read: the columns fill in a moment later.
    const SongMeta *m = &song->meta;
    fontDrawClipped(FONT_REGULAR, TEXT_COLUMN, COLUMN_ARTIST_X, y + 10, COLUMN_ALBUM_X - COLUMN_ARTIST_X - COLUMN_GAP,
                    m->artist, COLOR_COLUMN);
    // No album tag: the folder is the best guess.
    fontDrawClipped(FONT_REGULAR, TEXT_COLUMN, COLUMN_ALBUM_X, y + 10, COLUMN_END - COLUMN_ALBUM_X,
                    m->album[0] ? m->album : song->folder, m->album[0] ? COLOR_COLUMN : COLOR_DIM);
}

void uiDrawTrackRow(const TrackRow *row)
{
    static UiSong song;  // Big; the UI draws on one thread.
    char digits[24];
    uiLoadSong(row->path, &song);
    int y = row->y;
    if (row->selected)
        uiDrawSelection(y, COLOR_ACCENT);

    if (row->current) {
        uiDrawPlayingBars(MARGIN + 26, y + 12, row->playing, row->now_ns);
    } else {
        snprintf(digits, sizeof(digits), "%zu", row->number);
        int number_w = fontTextWidth(FONT_REGULAR, TEXT_SMALL, digits);
        fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN + 46 - number_w, y + 11, digits, COLOR_FAINT);
    }
    bool selected = row->selected && g_ui_list_focused;
    uiDrawSongTitle(&song, row->current || selected ? FONT_BOLD : FONT_REGULAR, MARGIN + 66, y + 8,
                    COLUMN_ARTIST_X - COLUMN_GAP - (MARGIN + 66), row->current ? COLOR_ACCENT : COLOR_TEXT);
    uiDrawSongColumns(&song, y);
    if (row->remove_button)
        uiDrawRemoveButton(GFX_WIDTH - MARGIN - 16 - ADD_BUTTON_W + 8, y, row->index, row->add_focused);
    else
        uiDrawAddButton(GFX_WIDTH - MARGIN - 16 - ADD_BUTTON_W + 8, y, row->index, row->add_focused);
}

void uiHitListArea(void)
{
    uiHitAdd(0, LIST_Y, GFX_WIDTH, UI_LIST_ROWS * ROW_H, HIT_LIST, 0);
}

void uiHitRow(int y, size_t index)
{
    uiHitAdd(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12), ROW_H, HIT_ROW, (int)index);
}

void uiDrawEmptyList(const char *subtitle, const char *hint)
{
    fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y, subtitle, COLOR_DIM);
    fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10, hint, COLOR_DIM);
}
