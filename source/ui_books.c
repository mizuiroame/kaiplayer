// The Audiobooks tab, and Now Playing for a book: its chapter's progress and
// the whole book's, ±30 s and chapters, the speed and the sleep timer.
#include <stdio.h>
#include <string.h>

#include "bookscan.h"
#include "covercache.h"
#include "lang.h"
#include "tempo.h"
#include "ui.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "ui_list.h"

#define ICON_X (MARGIN + 4)
#define NAME_X (MARGIN + 52)
#define ICON_SIZE 30
#define AUTHOR_X 600
#define PROGRESS_X 900
#define PROGRESS_W 120
#define LEFT_X (PROGRESS_X + PROGRESS_W + 16)
#define REST_ALPHA 70
// Now Playing's right side, as ui_now.c lays it out.
#define INFO_X 560
#define INFO_W 640
#define CHAPTER_BAR_Y 334
#define BOOK_BAR_Y 392
#define TRANSPORT_Y 476
#define TOGGLES_Y 564
#define SKIP_GAP 105
#define NS_PER_MINUTE 60000000000ull

// "3 h 05 min", "25 min".
static void formatLength(char *out, size_t size, double seconds)
{
    long minutes = seconds > 0.0 ? (long)(seconds / 60.0 + 0.5) : 0;
    if (minutes >= 60)
        snprintf(out, size, T("%ld h %02ld min"), minutes / 60, minutes % 60);
    else
        snprintf(out, size, T("%ld min"), minutes);
}

bool uiBookNow(const App *app, const PlayerStatus *status, UiBookNow *out)
{
    memset(out, 0, sizeof(*out));
    if (!appBookPlaying(app, status))
        return false;
    const Book *b = &app->books.book;
    double at = bookTimeAt(b, status->track, status->position);
    if (app->scrubbing && app->scrub_book) {  // The finger on the chapter's bar: where it will go.
        size_t ch = bookChapterIndex(b, at);
        double start = b->chapters.count ? b->chapters.items[ch].start : 0.0;
        at = start + (bookChapterEnd(b, ch) - start) * app->scrub_fraction;
    }
    out->book = b;
    out->at = at;
    out->speed = status->speed > 0 ? status->speed : TEMPO_PERMILLE;
    out->chapter = bookChapterIndex(b, at);
    out->chapter_count = b->chapters.count;
    out->chapter_start = b->chapters.count ? b->chapters.items[out->chapter].start : 0.0;
    out->chapter_end = bookChapterEnd(b, out->chapter);
    out->chapter_title = b->chapters.count ? b->chapters.items[out->chapter].title : b->title;
    return true;
}

// --- The tab ---

static void drawCoverIcon(int x, int y, const char *path)
{
    const CoverImage *cover = path ? coverCacheGet(path, true) : NULL;
    if (cover) {
        gfxBlitRounded(x, y, ICON_SIZE, cover->size, cover->pixels, 4);
        return;
    }
    // A closed book while its cover loads (or it has none).
    gfxFillRoundRect(x + 3, y + 1, ICON_SIZE - 6, ICON_SIZE - 2, 3, COLOR_SURFACE_HI);
    gfxFillRect(x + 7, y + 1, 3, ICON_SIZE - 2, COLOR_ACCENT);
    gfxFillRect(x + 13, y + 8, 10, 2, COLOR_DIM);
    gfxFillRect(x + 13, y + 13, 8, 2, COLOR_DIM);
}

// The bar and the words for how far a book has got.
static void drawBookProgress(const App *app, const Book *b, int y)
{
    const BookMark *m = appBooksMark(app, b->key);
    char text[96];
    double fraction = 0.0;
    Color color = COLOR_DIM;
    if (m && m->finished) {
        fraction = 1.0;
        snprintf(text, sizeof(text), "%s", T("Finished"));
    } else if (!b->probed) {
        snprintf(text, sizeof(text), "...");
    } else if (m && b->duration > 0.0) {
        double at = bookTimeAt(b, m->part, m->position);
        fraction = at / b->duration;
        char left[48];
        formatLength(left, sizeof(left), b->duration - at);
        snprintf(text, sizeof(text), T("%s left"), left);
        color = COLOR_TEXT;
    } else {
        formatLength(text, sizeof(text), b->duration);
    }
    fraction = fraction < 0.0 ? 0.0 : fraction > 1.0 ? 1.0 : fraction;
    int bar_y = y + ROW_H / 2 - 2;
    gfxBlendRect(PROGRESS_X, bar_y, PROGRESS_W, 4, COLOR_TEXT, REST_ALPHA);
    if (fraction > 0.0)
        gfxFillRoundRect(PROGRESS_X, bar_y, (int)(PROGRESS_W * fraction), 4, 2,
                         m && m->finished ? COLOR_DIM : COLOR_ACCENT);
    fontDrawClipped(FONT_REGULAR, 18, LEFT_X, y + 11, GFX_WIDTH - MARGIN - 8 - LEFT_X, text, color);
}

static void drawBookRow(const App *app, const Book *b, size_t row, int y, bool playing, const PlayerStatus *status,
                        uint64_t now_ns)
{
    bool selected = row == app->books.cursor;
    uiHitRow(y, row);
    if (selected)
        uiDrawSelection(y, COLOR_ACCENT);
    int icon_y = y + (ROW_H - ICON_SIZE) / 2;
    if (playing)
        uiDrawPlayingBars(ICON_X + 5, y + 12, status->state == PLAYER_PLAYING, now_ns);
    else
        drawCoverIcon(ICON_X, icon_y, b->part_count ? b->parts[0].path : NULL);
    fontDrawClipped(selected || playing ? FONT_BOLD : FONT_REGULAR, TEXT_ROW, NAME_X, y + 8, AUTHOR_X - 24 - NAME_X,
                    b->title, playing ? COLOR_ACCENT : COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 20, AUTHOR_X, y + 10, PROGRESS_X - 24 - AUTHOR_X, b->author, COLOR_DIM);
    drawBookProgress(app, b, y);
}

void uiDrawBooks(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    const BookList *l = bookScanLock();
    size_t count = appBooksCount(app);
    bool busy = bookScanBusy();
    char line[96];
    snprintf(line, sizeof(line), count == 1 ? T("%zu audiobook") : T("%zu audiobooks"), count);
    const char *looking = T("Looking for audiobooks...");
    int looking_w = busy ? fontTextWidth(FONT_REGULAR, TEXT_SMALL, looking) : 0;
    if (count == 0) {  // Where books are looked for.
        uiDrawEmptyList(busy ? looking : T("No audiobooks yet"),
                        T("Put them in sdmc:/audiobooks: a folder for each book, or .m4b files."));
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10 + ROW_H,
                 T("Folders named Audiobooks in the library, and Navidrome's Audiobook genre, show here too."), COLOR_DIM);
        bookScanUnlock();
        return;
    }
    fontDraw(FONT_BOLD, TEXT_SMALL, MARGIN, SUBTITLE_Y, line, COLOR_DIM);
    if (busy)
        fontDraw(FONT_REGULAR, TEXT_SMALL, GFX_WIDTH - MARGIN - looking_w, SUBTITLE_Y, looking, COLOR_ACCENT);
    const char *playing_key = appBookPlaying(app, status) ? app->books.book.key : NULL;
    int offset;
    size_t first = uiListBegin(&app->books.cursor, app->books.cursor, count, &offset);
    uiHitListArea();
    for (size_t i = first; i < count && i <= first + UI_LIST_ROWS; i++) {
        const Book *b = appBooksShown(app, l, i);
        if (b)
            drawBookRow(app, b, i, LIST_Y + (int)(i - first) * ROW_H - offset,
                        playing_key && strcmp(playing_key, b->key) == 0, status, now_ns);
    }
    uiListEnd();
    uiDrawScrollbar(first, count);
    bookScanUnlock();
}

// --- Now Playing for a book ---

// A circle open at the top with its arrow, and "30" inside: back (left) or on.
static void drawSkipIcon(int cx, int cy, bool forward, Color color)
{
    const float r = 19.0f, pi = 3.14159265f;
    float start = forward ? -pi / 2 + 0.55f : -pi / 2 - 0.55f;
    float sweep = 2 * pi - 1.3f;
    if (forward)
        gfxArc((float)cx, (float)cy, r, 3.0f, start, start + sweep, color);
    else
        gfxArc((float)cx, (float)cy, r, 3.0f, start - sweep, start, color);
    float tip_x = (float)cx + (forward ? 9.0f : -9.0f), tip_y = (float)cy - r;
    float back = forward ? -8.0f : 8.0f;
    gfxTriangle(tip_x, tip_y, tip_x + back, tip_y - 6.5f, tip_x + back, tip_y + 6.5f, color);
    fontDrawCentered(FONT_BOLD, 15, (float)cx, (float)cy + 1.0f, "30", color);
}

static void drawChapterSkip(int cx, int cy, bool next, Color color)
{
    if (next) {
        gfxFillTriangleRight(cx - 14, cy - 13, 22, 26, color);
        gfxFillRect(cx + 9, cy - 13, 5, 26, color);
    } else {
        gfxFillTriangleLeft(cx - 8, cy - 13, 22, 26, color);
        gfxFillRect(cx - 14, cy - 13, 5, 26, color);
    }
}

static void drawHint(int cx, int y, const char *key)
{
    uiDrawButtonCentered((float)cx, (float)y + 11.0f, key, COLOR_SURFACE_HI, COLOR_TEXT);
}

static void drawTransport(const PlayerStatus *status)
{
    int cx = INFO_X + INFO_W / 2, cy = TRANSPORT_Y;
    drawChapterSkip(cx - 2 * SKIP_GAP, cy, false, COLOR_TEXT);
    drawSkipIcon(cx - SKIP_GAP, cy, false, COLOR_TEXT);
    drawSkipIcon(cx + SKIP_GAP, cy, true, COLOR_TEXT);
    drawChapterSkip(cx + 2 * SKIP_GAP, cy, true, COLOR_TEXT);
    gfxFillCircle(cx, cy, 38, COLOR_ACCENT);
    if (status->state == PLAYER_PLAYING) {
        gfxFillRoundRect(cx - 12, cy - 15, 8, 30, 2, COLOR_BG);
        gfxFillRoundRect(cx + 4, cy - 15, 8, 30, 2, COLOR_BG);
    } else {
        gfxFillTriangleRight(cx - 9, cy - 16, 26, 32, COLOR_BG);
    }
    uiHitAdd(cx - 2 * SKIP_GAP - 44, cy - 40, 88, 80, HIT_PREV, 0);
    uiHitAdd(cx - SKIP_GAP - 44, cy - 40, 88, 80, HIT_BOOK_BACK, 0);
    uiHitAdd(cx - 44, cy - 44, 88, 88, HIT_PLAY, 0);
    uiHitAdd(cx + SKIP_GAP - 44, cy - 40, 88, 80, HIT_BOOK_FORWARD, 0);
    uiHitAdd(cx + 2 * SKIP_GAP - 44, cy - 40, 88, 80, HIT_NEXT, 0);
    drawHint(cx - 2 * SKIP_GAP, cy + 26, "L");
    drawHint(cx - SKIP_GAP, cy + 26, "<");
    drawHint(cx, cy + 46, "A");
    drawHint(cx + SKIP_GAP, cy + 26, ">");
    drawHint(cx + 2 * SKIP_GAP, cy + 26, "R");
}

// The chapter's bar (a tap or a drag moves in the chapter) and its times; the time left is at the speed.
static void drawChapterBar(const UiBookNow *n)
{
    double length = n->chapter_end - n->chapter_start, in = n->at - n->chapter_start;
    double fraction = length > 0.0 ? in / length : 0.0;
    fraction = fraction < 0.0 ? 0.0 : fraction > 1.0 ? 1.0 : fraction;
    int filled = (int)(INFO_W * fraction);
    gfxBlendRect(INFO_X, CHAPTER_BAR_Y + 1, INFO_W, 4, COLOR_TEXT, REST_ALPHA);
    uiHitAdd(INFO_X - 12, CHAPTER_BAR_Y - 18, INFO_W + 24, 42, HIT_BOOK_SEEK, 0);  // 12 px slack: app_touch.c
    if (filled > 0)
        gfxFillRoundRect(INFO_X, CHAPTER_BAR_Y, filled, 6, 3, COLOR_ACCENT);
    gfxFillCircle(INFO_X + filled, CHAPTER_BAR_Y + 3, 8, COLOR_TEXT);
    char elapsed[24], left[24];
    utilFormatTime(elapsed, sizeof(elapsed), in);
    left[0] = '-';
    utilFormatTime(left + 1, sizeof(left) - 1, (length - in) * TEMPO_PERMILLE / n->speed);
    fontDraw(FONT_REGULAR, 17, INFO_X, CHAPTER_BAR_Y + 16, elapsed, COLOR_DIM);
    fontDraw(FONT_REGULAR, 17, INFO_X + INFO_W - fontTextWidth(FONT_REGULAR, 17, left), CHAPTER_BAR_Y + 16, left,
             COLOR_DIM);
}

// The whole book: a thin bar, how far in, and how long is left at the speed.
static void drawBookBar(const UiBookNow *n)
{
    const Book *b = n->book;
    double fraction = b->duration > 0.0 ? n->at / b->duration : 0.0;
    fraction = fraction < 0.0 ? 0.0 : fraction > 1.0 ? 1.0 : fraction;
    gfxBlendRect(INFO_X, BOOK_BAR_Y, INFO_W, 3, COLOR_TEXT, 45);
    if (fraction > 0.0)
        gfxFillRect(INFO_X, BOOK_BAR_Y, (int)(INFO_W * fraction), 3, gfxMix(COLOR_TEXT, COLOR_ACCENT, 140));
    char at[24], total[24], line[96], left[48], right[96];
    utilFormatTime(at, sizeof(at), n->at);
    utilFormatTime(total, sizeof(total), b->duration);
    snprintf(line, sizeof(line), T("Book: %s of %s  ·  %d%%"), at, total, (int)(fraction * 100.0));
    formatLength(left, sizeof(left), (b->duration - n->at) * TEMPO_PERMILLE / n->speed);
    snprintf(right, sizeof(right), T("%s left"), left);
    fontDraw(FONT_REGULAR, 17, INFO_X, BOOK_BAR_Y + 12, line, COLOR_DIM);
    fontDraw(FONT_REGULAR, 17, INFO_X + INFO_W - fontTextWidth(FONT_REGULAR, 17, right), BOOK_BAR_Y + 12, right,
             COLOR_DIM);
}

static void drawToggles(const App *app, const UiBookNow *n, uint64_t now_ns)
{
    int x = INFO_X;
    char speed[16];
    tempoFormatSpeed(speed, sizeof(speed), n->speed);
    int w = uiIconPillWidth("Y", speed);
    uiDrawIconPill(x, TOGGLES_Y, "Y", PILL_ICON_SPEED, speed, n->speed != TEMPO_PERMILLE);
    uiHitAdd(x, TOGGLES_Y - 8, w, 46, HIT_BOOK_SPEED, 0);
    x += w + 12;
    const char *chapters = T("Chapters");
    w = uiIconPillWidth("X", chapters);
    uiDrawIconPill(x, TOGGLES_Y, "X", PILL_ICON_CHAPTERS, chapters, false);
    uiHitAdd(x, TOGGLES_Y - 8, w, 46, HIT_BOOK_CHAPTERS, 0);
    x += w + 12;
    char sleep[48];
    bool on = true;
    if (app->books.sleep_chapter)
        snprintf(sleep, sizeof(sleep), "%s", T("End of chapter"));
    else if (app->sleep_deadline_ns > now_ns)
        snprintf(sleep, sizeof(sleep), T("%d min"),
                 (int)((app->sleep_deadline_ns - now_ns + NS_PER_MINUTE - 1) / NS_PER_MINUTE));
    else {
        snprintf(sleep, sizeof(sleep), "%s", T("Sleep timer"));
        on = false;
    }
    w = uiIconPillWidth("^", sleep);
    uiDrawIconPill(x, TOGGLES_Y, "^", PILL_ICON_SLEEP, sleep, on);
    uiHitAdd(x, TOGGLES_Y - 8, w, 46, HIT_BOOK_SLEEP, 0);
}

void uiDrawBookNow(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    UiBookNow n;
    if (!uiBookNow(app, status, &n))
        return;
    const Book *b = n.book;
    fontDraw(FONT_BOLD, 15, INFO_X, 150, status->state == PLAYER_PAUSED ? T("PAUSED") : T("AUDIOBOOK"), COLOR_DIM);
    fontDrawClipped(FONT_BOLD, 40, INFO_X, 172, INFO_W, b->title, COLOR_TEXT);
    if (b->author[0])
        fontDrawClipped(FONT_REGULAR, 24, INFO_X, 230, INFO_W, b->author, COLOR_TEXT);
    char where[64];
    snprintf(where, sizeof(where), T("Chapter %zu of %zu"), n.chapter + 1, n.chapter_count ? n.chapter_count : 1);
    int w = fontDraw(FONT_REGULAR, 20, INFO_X, 276, where, COLOR_DIM);
    fontDraw(FONT_REGULAR, 20, INFO_X + w, 276, "  \xC2\xB7  ", COLOR_DIM);
    w += fontTextWidth(FONT_REGULAR, 20, "  \xC2\xB7  ");
    fontDrawClipped(FONT_BOLD, 20, INFO_X + w, 276, INFO_W - w, n.chapter_title, COLOR_TEXT);
    drawChapterBar(&n);
    drawBookBar(&n);
    drawTransport(status);
    drawToggles(app, &n, now_ns);
}
