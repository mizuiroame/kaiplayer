#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "util.h"

#define COLOR_BG GFX_RGB(0x10, 0x12, 0x18)
#define COLOR_PANEL GFX_RGB(0x18, 0x1B, 0x24)
#define COLOR_ROW_SELECTED GFX_RGB(0x2A, 0x2F, 0x45)
#define COLOR_ACCENT GFX_RGB(0xFF, 0x6B, 0x9A)
#define COLOR_FOLDER GFX_RGB(0x7C, 0xC4, 0xFF)
#define COLOR_TEXT GFX_RGB(0xE8, 0xE8, 0xF0)
#define COLOR_DIM GFX_RGB(0x8A, 0x8F, 0xA8)
#define COLOR_TRACK GFX_RGB(0x33, 0x37, 0x48)
#define COLOR_ERROR GFX_RGB(0xFF, 0x8A, 0x5C)

#define MARGIN 32
#define HEADER_H 60
#define SUBTITLE_Y 74
#define LIST_Y 104
#define ROW_H 34
#define PLAYER_Y 560
#define PROGRESS_Y 646
#define FOOTER_Y 684
#define TEXT_SMALL 2
#define TEXT_LARGE 3

static void stripExtension(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s", name);
    char *dot = strrchr(out, '.');
    if (dot && dot != out)
        *dot = '\0';
}

// First row to show so that cursor stays near the middle.
static size_t firstVisible(size_t cursor, size_t count)
{
    if (count <= UI_LIST_ROWS || cursor < UI_LIST_ROWS / 2)
        return 0;
    size_t first = cursor - UI_LIST_ROWS / 2;
    return first + UI_LIST_ROWS > count ? count - UI_LIST_ROWS : first;
}

static void drawFolderIcon(int x, int y, Color color)
{
    gfxFillRect(x, y + 2, 8, 3, color);
    gfxFillRect(x, y + 4, 20, 13, color);
}

static void drawNoteIcon(int x, int y, Color color)
{
    gfxFillRect(x + 2, y + 11, 7, 6, color);
    gfxFillRect(x + 7, y, 2, 13, color);
    gfxFillRect(x + 9, y, 6, 3, color);
}

static void drawRow(bool selected, int y)
{
    if (selected) {
        gfxFillRect(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12), ROW_H, COLOR_ROW_SELECTED);
        gfxFillRect(MARGIN - 12, y, 4, ROW_H, COLOR_ACCENT);
    }
}

static void drawScrollbar(size_t first, size_t count)
{
    if (count <= UI_LIST_ROWS)
        return;
    int track_h = UI_LIST_ROWS * ROW_H;
    int thumb_h = track_h * UI_LIST_ROWS / (int)count;
    if (thumb_h < 16)
        thumb_h = 16;
    int thumb_y = LIST_Y + (int)((long)(track_h - thumb_h) * (long)first / (long)(count - UI_LIST_ROWS));
    gfxFillRect(GFX_WIDTH - 14, LIST_Y, 4, track_h, COLOR_PANEL);
    gfxFillRect(GFX_WIDTH - 14, thumb_y, 4, thumb_h, COLOR_DIM);
}

static void drawBrowser(const App *app)
{
    const Browser *b = &app->browser;
    gfxDrawTextClipped(MARGIN, SUBTITLE_Y, GFX_WIDTH - 2 * MARGIN, b->path, TEXT_SMALL, COLOR_DIM);

    if (b->count == 0) {
        gfxDrawText(MARGIN, LIST_Y + 8, "Empty folder: no subfolders or music.", TEXT_SMALL, COLOR_DIM);
        return;
    }
    size_t first = firstVisible(b->cursor, b->count);
    for (size_t i = first; i < b->count && i < first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H;
        const BrowserEntry *e = &b->entries[i];
        drawRow(i == b->cursor, y);
        if (e->is_dir)
            drawFolderIcon(MARGIN, y + 8, COLOR_FOLDER);
        else
            drawNoteIcon(MARGIN + 2, y + 8, COLOR_ACCENT);
        gfxDrawTextClipped(MARGIN + 34, y + 9, GFX_WIDTH - 2 * MARGIN - 50, e->name, TEXT_SMALL,
                           e->is_dir ? COLOR_FOLDER : COLOR_TEXT);
    }
    drawScrollbar(first, b->count);
}

static void drawQueue(const App *app, const PlayerStatus *status)
{
    char line[UTIL_PATH_MAX];
    if (status->track_count == 0) {
        gfxDrawText(MARGIN, SUBTITLE_Y, "Queue is empty", TEXT_SMALL, COLOR_DIM);
        gfxDrawText(MARGIN, LIST_Y + 8, "Pick a folder in the browser (press B to go back).",
                    TEXT_SMALL, COLOR_DIM);
        return;
    }
    snprintf(line, sizeof(line), "Queue: %zu tracks", status->track_count);
    gfxDrawText(MARGIN, SUBTITLE_Y, line, TEXT_SMALL, COLOR_DIM);

    size_t cursor = app->queue_cursor < status->track_count ? app->queue_cursor : 0;
    size_t first = firstVisible(cursor, status->track_count);
    for (size_t i = first; i < status->track_count && i < first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H;
        bool current = i == status->track && status->state != PLAYER_STOPPED;
        drawRow(i == cursor, y);
        char name[UTIL_PATH_MAX], shown[UTIL_PATH_MAX];
        if (!playerTrackName(i, name, sizeof(name)))
            break;
        stripExtension(shown, sizeof(shown), name);
        snprintf(line, sizeof(line), "%3zu", i + 1);
        gfxDrawText(MARGIN, y + 9, line, TEXT_SMALL, COLOR_DIM);
        if (current)
            gfxFillPlayIcon(MARGIN + 60, y + 9, 12, 16, COLOR_ACCENT);
        gfxDrawTextClipped(MARGIN + 84, y + 9, GFX_WIDTH - 2 * MARGIN - 100, shown, TEXT_SMALL,
                           current ? COLOR_ACCENT : COLOR_TEXT);
    }
    drawScrollbar(first, status->track_count);
}

static void drawHeader(const App *app)
{
    gfxFillRect(0, 0, GFX_WIDTH, HEADER_H, COLOR_PANEL);
    gfxDrawText(MARGIN, 18, "KaiPlayer", TEXT_LARGE, COLOR_ACCENT);

    const char *tabs[] = {"Browse", "Queue"};
    int x = GFX_WIDTH - MARGIN;
    for (int i = 1; i >= 0; i--) {
        int w = gfxTextWidth(tabs[i], TEXT_SMALL);
        x -= w;
        bool active = (int)app->view == i;
        gfxDrawText(x, 22, tabs[i], TEXT_SMALL, active ? COLOR_TEXT : COLOR_DIM);
        if (active)
            gfxFillRect(x, 44, w, 3, COLOR_ACCENT);
        x -= 32;
    }
}

static const char *repeatLabel(RepeatMode r)
{
    switch (r) {
    case REPEAT_ALL: return "Repeat: all";
    case REPEAT_ONE: return "Repeat: one";
    default: return "Repeat: off";
    }
}

static void drawPlayerBar(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    gfxFillRect(0, PLAYER_Y, GFX_WIDTH, FOOTER_Y - PLAYER_Y, COLOR_PANEL);

    char name[UTIL_PATH_MAX], title[UTIL_PATH_MAX], line[256];
    bool has_track = status->track_count > 0 && playerTrackName(status->track, name, sizeof(name));
    if (has_track)
        stripExtension(title, sizeof(title), name);
    else
        snprintf(title, sizeof(title), "%s", "Nothing playing");

    int icon_x = MARGIN;
    if (status->state == PLAYER_PLAYING) {
        gfxFillPlayIcon(icon_x, PLAYER_Y + 16, 18, 24, COLOR_ACCENT);
    } else {
        gfxFillRect(icon_x, PLAYER_Y + 16, 6, 24, COLOR_DIM);
        gfxFillRect(icon_x + 12, PLAYER_Y + 16, 6, 24, COLOR_DIM);
    }
    gfxDrawTextClipped(MARGIN + 36, PLAYER_Y + 16, GFX_WIDTH - 2 * MARGIN - 36, title, TEXT_LARGE,
                       has_track ? COLOR_TEXT : COLOR_DIM);

    if (app->toast[0] && now_ns < app->toast_until_ns) {
        gfxDrawTextClipped(MARGIN + 36, PLAYER_Y + 52, GFX_WIDTH - 2 * MARGIN - 36,
                           app->toast, TEXT_SMALL, COLOR_ERROR);
    } else {
        snprintf(line, sizeof(line), "%zu/%zu   Shuffle: %s   %s   Volume: %d%%",
                 has_track ? status->track + 1 : 0, status->track_count,
                 status->shuffle ? "on" : "off", repeatLabel(status->repeat),
                 (int)(status->volume * 100.0f + 0.5f));
        gfxDrawText(MARGIN + 36, PLAYER_Y + 52, line, TEXT_SMALL, COLOR_DIM);
    }

    char elapsed[16], total[16];
    utilFormatTime(elapsed, sizeof(elapsed), status->position);
    utilFormatTime(total, sizeof(total), status->duration);
    int bar_x = MARGIN + 100, bar_w = GFX_WIDTH - 2 * MARGIN - 200;
    gfxDrawText(MARGIN, PROGRESS_Y - 4, elapsed, TEXT_SMALL, COLOR_TEXT);
    gfxDrawText(GFX_WIDTH - MARGIN - gfxTextWidth(total, TEXT_SMALL), PROGRESS_Y - 4,
                status->duration > 0 ? total : "--:--", TEXT_SMALL, COLOR_DIM);
    gfxFillRect(bar_x, PROGRESS_Y, bar_w, 8, COLOR_TRACK);
    if (status->duration > 0) {
        double fraction = status->position / status->duration;
        if (fraction > 1.0)
            fraction = 1.0;
        gfxFillRect(bar_x, PROGRESS_Y, (int)(bar_w * fraction), 8, COLOR_ACCENT);
    }
}

static void drawFooter(const App *app)
{
    const char *help = app->view == VIEW_BROWSER
        ? "A open  B up  X play folder  Y queue  - pause  L/R track  ZL/ZR vol  + exit"
        : "A play  B back  X shuffle  Y repeat  - pause  L/R track  RS seek  + exit";
    gfxDrawTextClipped(MARGIN, FOOTER_Y + 12, GFX_WIDTH - 2 * MARGIN, help, TEXT_SMALL, COLOR_DIM);
}

void uiDraw(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    gfxClear(COLOR_BG);
    drawHeader(app);
    if (!app->audio_ok) {
        gfxDrawText(MARGIN, SUBTITLE_Y, "Audio unavailable: audout could not start.",
                    TEXT_SMALL, COLOR_ERROR);
    } else if (app->view == VIEW_BROWSER) {
        drawBrowser(app);
    } else {
        drawQueue(app, status);
    }
    drawPlayerBar(app, status, now_ns);
    drawFooter(app);
}
