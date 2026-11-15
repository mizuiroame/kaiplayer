#include "ui_list.h"

#include <stdio.h>

#include "ui.h"
#include "ui_common.h"

#define TRACK_RIGHT_EDGE (GFX_WIDTH - MARGIN - 16)
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

void uiDrawTrackRow(size_t number, const char *path, int y, bool selected, bool current)
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
}

void uiDrawEmptyList(const char *subtitle, const char *hint)
{
    fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y, subtitle, COLOR_DIM);
    fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10, hint, COLOR_DIM);
}
