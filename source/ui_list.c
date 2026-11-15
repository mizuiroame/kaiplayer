#include "ui_list.h"

#include <stdio.h>

#include "ui.h"
#include "ui_common.h"
#include "ui_hit.h"

#define TRACK_RIGHT_EDGE (GFX_WIDTH - MARGIN - 16 - ADD_BUTTON_W)
#define TRACK_FOLDER_MAX_W 360

size_t uiFirstVisible(size_t cursor, size_t count)
{
    if (count <= UI_LIST_ROWS || cursor < UI_LIST_ROWS / 2)
        return 0;
    size_t first = cursor - UI_LIST_ROWS / 2;
    return first + UI_LIST_ROWS > count ? count - UI_LIST_ROWS : first;
}

void uiDrawSelection(int y, Color bar)
{
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
    int thumb_y = LIST_Y + (int)((long)(track_h - thumb_h) * (long)first / (long)(count - UI_LIST_ROWS));
    gfxFillRoundRect(GFX_WIDTH - 12, LIST_Y, 4, track_h, 2, COLOR_SURFACE);
    gfxFillRoundRect(GFX_WIDTH - 12, thumb_y, 4, thumb_h, 2, COLOR_FAINT);
}

void uiDrawAddButton(int x, int y, size_t index)
{
    int cx = x + ADD_BUTTON_W / 2, cy = y + ROW_H / 2;
    gfxFillCircle(cx, cy, 14, COLOR_SURFACE_HI);
    gfxFillRect(cx - 7, cy - 1, 14, 3, COLOR_DIM);
    gfxFillRect(cx - 1, cy - 7, 3, 14, COLOR_DIM);
    uiHitAdd(x, y, ADD_BUTTON_W, ROW_H, HIT_ADD, (int)index);
}

int uiDrawBackButton(int x, int y)
{
    static const char LABEL[] = "Back";
    int w = fontTextWidth(FONT_BOLD, 17, LABEL) + 44;
    gfxFillRoundRect(x, y, w, 30, 15, COLOR_SURFACE_HI);
    gfxFillTriangleLeft(x + 12, y + 8, 9, 14, COLOR_TEXT);
    fontDraw(FONT_BOLD, 17, x + 28, y + 4, LABEL, COLOR_TEXT);
    uiHitAdd(x - 8, y - 10, w + 16, 50, HIT_BACK, 0);
    return w;
}

void uiDrawTrackRow(size_t number, const char *path, int y, bool selected, bool current, size_t index)
{
    char title[UTIL_PATH_MAX], folder[UTIL_PATH_MAX], digits[24];
    uiSplitTrackPath(path, title, folder);
    if (selected)
        uiDrawSelection(y, COLOR_ACCENT);

    snprintf(digits, sizeof(digits), "%zu", number);
    int number_w = fontTextWidth(FONT_REGULAR, TEXT_SMALL, digits);
    if (current)
        gfxFillTriangleRight(MARGIN + 34, y + 14, 12, 16, COLOR_ACCENT);
    else
        fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN + 46 - number_w, y + 11, digits, COLOR_FAINT);

    int folder_w = fontTextWidth(FONT_REGULAR, TEXT_SMALL, folder);
    if (folder_w > TRACK_FOLDER_MAX_W)
        folder_w = TRACK_FOLDER_MAX_W;
    int folder_x = TRACK_RIGHT_EDGE - folder_w;
    fontDrawClipped(FONT_REGULAR, TEXT_SMALL, folder_x, y + 11, folder_w, folder, COLOR_DIM);
    fontDrawClipped(current || selected ? FONT_BOLD : FONT_REGULAR, TEXT_ROW, MARGIN + 66, y + 8,
                    folder_x - (MARGIN + 66) - 24, title, current ? COLOR_ACCENT : COLOR_TEXT);
    uiDrawAddButton(GFX_WIDTH - MARGIN - 16 - ADD_BUTTON_W + 8, y, index);
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
