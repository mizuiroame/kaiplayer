#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "ui_common.h"

#define SUBTITLE_Y 76
#define LIST_Y 108
#define ROW_H 44
#define MINI_Y 600
#define MINI_H 72
#define THUMB_SIZE ARTWORK_SMALL_SIZE
#define TEXT_ROW 22
#define TEXT_SMALL 18
#define QUEUE_RIGHT_EDGE (GFX_WIDTH - MARGIN - 16)
#define QUEUE_FOLDER_MAX_W 360
#define TAB_GAP 28
#define BATTERY_LOW 15
#define COLOR_CHARGING GFX_RGB(0x5B, 0xD6, 0x7A)

static const char *const VIEW_NAMES[VIEW_COUNT] = {"Browse", "Queue", "Now Playing"};

// First row to show so that the cursor stays near the middle.
static size_t firstVisible(size_t cursor, size_t count)
{
    if (count <= UI_LIST_ROWS || cursor < UI_LIST_ROWS / 2)
        return 0;
    size_t first = cursor - UI_LIST_ROWS / 2;
    return first + UI_LIST_ROWS > count ? count - UI_LIST_ROWS : first;
}

static void drawSelection(int y)
{
    gfxFillRoundRect(MARGIN - 12, y + 2, GFX_WIDTH - 2 * (MARGIN - 12), ROW_H - 4, 8, COLOR_SURFACE_HI);
    gfxFillRect(MARGIN - 12, y + 10, 3, ROW_H - 20, COLOR_ACCENT);
}

static void drawFolderIcon(int x, int y, Color color)
{
    gfxFillRoundRect(x, y, 10, 6, 2, color);
    gfxFillRoundRect(x, y + 3, 22, 15, 3, color);
}

static void drawScrollbar(size_t first, size_t count)
{
    if (count <= UI_LIST_ROWS)
        return;
    int track_h = UI_LIST_ROWS * ROW_H;
    int thumb_h = track_h * UI_LIST_ROWS / (int)count;
    if (thumb_h < 24)
        thumb_h = 24;
    int thumb_y = LIST_Y + (int)((long)(track_h - thumb_h) * (long)first / (long)(count - UI_LIST_ROWS));
    gfxFillRoundRect(GFX_WIDTH - 12, LIST_Y, 4, track_h, 2, COLOR_SURFACE);
    gfxFillRoundRect(GFX_WIDTH - 12, thumb_y, 4, thumb_h, 2, COLOR_FAINT);
}

static void drawBrowser(const App *app)
{
    const Browser *b = &app->browser;
    int path_w = GFX_WIDTH - 2 * MARGIN;
    if (b->incomplete) {
        // The console returned an error while listing: some names may be missing.
        static const char WARNING[] = "Some items in this folder could not be read";
        int warning_w = fontTextWidth(FONT_REGULAR, TEXT_SMALL, WARNING);
        fontDraw(FONT_REGULAR, TEXT_SMALL, GFX_WIDTH - MARGIN - warning_w, SUBTITLE_Y, WARNING, COLOR_ERROR);
        path_w -= warning_w + 24;
    }
    fontDrawClipped(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y, path_w, b->path, COLOR_DIM);
    if (b->count == 0) {
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10, "This folder has no subfolders or music.", COLOR_DIM);
        return;
    }
    size_t first = firstVisible(b->cursor, b->count);
    for (size_t i = first; i < b->count && i < first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H;
        const BrowserEntry *e = &b->entries[i];
        if (i == b->cursor)
            drawSelection(y);
        if (e->is_dir)
            drawFolderIcon(MARGIN + 4, y + 13, COLOR_FOLDER);
        else
            uiDrawNoteIcon(MARGIN + 4, y + 10, 24, COLOR_ACCENT);
        fontDrawClipped(i == b->cursor ? FONT_BOLD : FONT_REGULAR, TEXT_ROW, MARGIN + 44, y + 8,
                        GFX_WIDTH - 2 * MARGIN - 60, e->name, e->is_dir ? COLOR_FOLDER : COLOR_TEXT);
    }
    drawScrollbar(first, b->count);
}

static void drawQueueRow(size_t i, int y, bool selected, bool current)
{
    char path[UTIL_PATH_MAX], title[UTIL_PATH_MAX], folder[UTIL_PATH_MAX], number[24];
    if (!playerTrackPath(i, path, sizeof(path)))
        return;
    uiSplitTrackPath(path, title, folder);
    if (selected)
        drawSelection(y);

    snprintf(number, sizeof(number), "%zu", i + 1);
    int number_w = fontTextWidth(FONT_REGULAR, TEXT_SMALL, number);
    if (current)
        gfxFillTriangleRight(MARGIN + 34, y + 14, 12, 16, COLOR_ACCENT);
    else
        fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN + 46 - number_w, y + 11, number, COLOR_FAINT);

    int folder_w = fontTextWidth(FONT_REGULAR, TEXT_SMALL, folder);
    if (folder_w > QUEUE_FOLDER_MAX_W)
        folder_w = QUEUE_FOLDER_MAX_W;
    int folder_x = QUEUE_RIGHT_EDGE - folder_w;
    fontDrawClipped(FONT_REGULAR, TEXT_SMALL, folder_x, y + 11, folder_w, folder, COLOR_DIM);
    fontDrawClipped(current || selected ? FONT_BOLD : FONT_REGULAR, TEXT_ROW, MARGIN + 66, y + 8,
                    folder_x - (MARGIN + 66) - 24, title, current ? COLOR_ACCENT : COLOR_TEXT);
}

static void drawQueue(const App *app, const PlayerStatus *status)
{
    if (status->track_count == 0) {
        fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y, "Queue is empty", COLOR_DIM);
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10,
                 "Pick a folder in Browse and press X to play it.", COLOR_DIM);
        return;
    }
    char line[64];
    snprintf(line, sizeof(line), "%zu tracks", status->track_count);
    fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y, line, COLOR_DIM);

    size_t cursor = app->queue_cursor < status->track_count ? app->queue_cursor : 0;
    size_t first = firstVisible(cursor, status->track_count);
    for (size_t i = first; i < status->track_count && i < first + UI_LIST_ROWS; i++) {
        bool current = i == status->track && status->state != PLAYER_STOPPED;
        drawQueueRow(i, LIST_Y + (int)(i - first) * ROW_H, i == cursor, current);
    }
    drawScrollbar(first, status->track_count);
}

static void drawButtonHint(int x, int y, const char *label)
{
    int w = fontTextWidth(FONT_BOLD, 14, label) + 14;
    gfxFillRoundRect(x, y, w, 22, 11, COLOR_SURFACE_HI);
    fontDraw(FONT_BOLD, 14, x + 7, y + 2, label, COLOR_DIM);
}

// The tabs, centered, between the ZL and ZR hints.
static void drawTabs(const App *app)
{
    int widths[VIEW_COUNT], total = 0;
    for (int i = 0; i < VIEW_COUNT; i++) {
        widths[i] = fontTextWidth((int)app->view == i ? FONT_BOLD : FONT_REGULAR, 20, VIEW_NAMES[i]);
        total += widths[i] + (i > 0 ? TAB_GAP : 0);
    }
    int x = (GFX_WIDTH - total) / 2;
    drawButtonHint(x - 48, 21, "ZL");
    for (int i = 0; i < VIEW_COUNT; i++) {
        bool active = (int)app->view == i;
        fontDraw(active ? FONT_BOLD : FONT_REGULAR, 20, x, 18, VIEW_NAMES[i], active ? COLOR_TEXT : COLOR_DIM);
        if (active)
            gfxFillRoundRect(x, 48, widths[i], 3, 1, COLOR_ACCENT);
        x += widths[i] + TAB_GAP;
    }
    drawButtonHint(x - TAB_GAP + 16, 21, "ZR");
}

// Battery outline with a fill for the charge; green while charging.
static void drawBattery(int x, int y, int percent, bool charging)
{
    Color fill = charging ? COLOR_CHARGING : percent <= BATTERY_LOW ? COLOR_ERROR : COLOR_TEXT;
    gfxFillRoundRect(x, y, 30, 16, 4, COLOR_DIM);
    gfxFillRoundRect(x + 2, y + 2, 26, 12, 3, COLOR_BG);
    gfxFillRoundRect(x + 31, y + 5, 3, 6, 1, COLOR_DIM);
    int inner = 22 * (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100;
    if (inner > 0)
        gfxFillRoundRect(x + 4, y + 4, inner, 8, 2, fill);
}

// Clock and battery in the top right corner.
static void drawStatus(const SysInfo *sys)
{
    int x = GFX_WIDTH - MARGIN;
    if (sys->has_battery) {
        char percent[8];
        snprintf(percent, sizeof(percent), "%d%%", sys->battery_percent);
        x -= 34;
        drawBattery(x, 22, sys->battery_percent, sys->charging);
        x -= fontTextWidth(FONT_REGULAR, 17, percent) + 8;
        fontDraw(FONT_REGULAR, 17, x, 19, percent, COLOR_DIM);
        x -= 20;
    }
    if (sys->has_time) {
        char clock[8];
        snprintf(clock, sizeof(clock), "%02d:%02d", sys->hour % 24, sys->minute % 60);
        x -= fontTextWidth(FONT_BOLD, 18, clock);
        fontDraw(FONT_BOLD, 18, x, 18, clock, COLOR_TEXT);
    }
}

static void drawHeader(const App *app)
{
    fontDraw(FONT_BOLD, 26, MARGIN, 16, "KaiPlayer", COLOR_ACCENT);
    drawTabs(app);
    drawStatus(&app->sys);
}

static void drawMiniPlayer(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns)
{
    gfxFillRect(0, MINI_Y, GFX_WIDTH, MINI_H, COLOR_SURFACE);
    gfxFillRect(0, MINI_Y, GFX_WIDTH, 3, COLOR_FAINT);
    if (status->duration > 0) {
        double fraction = status->position / status->duration;
        gfxFillRect(0, MINI_Y, (int)(GFX_WIDTH * (fraction > 1.0 ? 1.0 : fraction)), 3, COLOR_ACCENT);
    }

    uiDrawCover(MARGIN - 8, MINI_Y + 10, THUMB_SIZE, track->thumb);
    int text_x = MARGIN + THUMB_SIZE + 8;
    int text_w = GFX_WIDTH - text_x - 300;
    fontDrawClipped(FONT_BOLD, 20, text_x, MINI_Y + 12, text_w, track->title,
                    track->has_track ? COLOR_TEXT : COLOR_DIM);

    bool toast = app->toast[0] && now_ns < app->toast_until_ns;
    const char *second = toast ? app->toast : track->artist[0] ? track->artist : track->folder;
    fontDrawClipped(FONT_REGULAR, 17, text_x, MINI_Y + 40, text_w, second, toast ? COLOR_ERROR : COLOR_DIM);

    char elapsed[16], total[16], line[96];
    utilFormatTime(elapsed, sizeof(elapsed), status->position);
    utilFormatTime(total, sizeof(total), status->duration);
    snprintf(line, sizeof(line), "%s %s / %s", status->state == PLAYER_PAUSED ? "Paused" : "",
             elapsed, status->duration > 0 ? total : "--:--");
    fontDraw(FONT_REGULAR, 18, GFX_WIDTH - MARGIN - fontTextWidth(FONT_REGULAR, 18, line),
             MINI_Y + 12, line, COLOR_DIM);
    snprintf(line, sizeof(line), "%s  ·  %s", status->shuffle ? "Shuffle on" : "Shuffle off",
             uiRepeatLabel(status->repeat));
    fontDraw(FONT_REGULAR, 16, GFX_WIDTH - MARGIN - fontTextWidth(FONT_REGULAR, 16, line),
             MINI_Y + 42, line, COLOR_FAINT);
}

static void drawFooter(const App *app)
{
    static const char *const HELP[VIEW_COUNT] = {
        "A open    B back    X play folder    Y shuffle folder    − pause    L/R track    + exit",
        "A play    B browse    X shuffle    Y repeat    − pause    L/R track    + exit",
        "A pause    Left/Right seek    X shuffle    Y repeat    L/R track    + exit",
    };
    fontDrawClipped(FONT_REGULAR, 16, MARGIN, FOOTER_Y + 16, GFX_WIDTH - 2 * MARGIN, HELP[app->view], COLOR_FAINT);
}

void uiDraw(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    UiTrack track;
    const Artwork *art = artworkLock();
    uiResolveTrack(status, art, &track);

    gfxClear(COLOR_BG);
    if (app->view == VIEW_NOW_PLAYING) {
        uiDrawNowPlaying(app, status, &track, now_ns);
    } else {
        if (!app->audio_ok)
            fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y,
                     "Audio unavailable: audout could not start.", COLOR_ERROR);
        else if (app->view == VIEW_BROWSER)
            drawBrowser(app);
        else
            drawQueue(app, status);
        drawMiniPlayer(app, status, &track, now_ns);
    }
    drawHeader(app);
    drawFooter(app);
    artworkUnlock();
}
