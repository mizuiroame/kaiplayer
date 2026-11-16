#include "ui_common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "meta.h"
#include "ui_buttons.h"

Color g_ui_accent = GFX_RGB(0xFF, 0x6B, 0x9A);

void uiSplitTrackPath(const char *path, char *title, char *folder)
{
    snprintf(title, UTIL_PATH_MAX, "%s", utilFileName(path));
    char *dot = strrchr(title, '.');
    if (dot && dot != title)
        *dot = '\0';

    snprintf(folder, UTIL_PATH_MAX, "%s", path);
    if (utilParentPath(folder)) {
        const char *name = utilFileName(folder);
        memmove(folder, name, strlen(name) + 1);
    } else {
        folder[0] = '\0';
    }
}

void uiResolveTrack(const PlayerStatus *status, const Artwork *art, UiTrack *track)
{
    memset(track, 0, sizeof(*track));
    char path[UTIL_PATH_MAX];
    track->has_track = status->track_count > 0 && playerTrackPath(status->track, path, sizeof(path));
    if (!track->has_track) {
        snprintf(track->title, sizeof(track->title), "%s", "Nothing playing");
        return;
    }
    uiSplitTrackPath(path, track->title, track->folder);
    if (art && strcmp(art->path, path) != 0) {
        // The new song's artwork is still loading: keep the last cover (and
        // so the accent) instead of flashing the placeholder, and take the
        // text from the list cache if it has it.
        if (art->has_cover) {
            track->cover = &art->cover;
            track->thumb = &art->thumb;
        }
        static SongMeta meta;  // Big; the UI draws on one thread.
        if (metaLookup(path, &meta)) {
            if (meta.title[0])
                snprintf(track->title, sizeof(track->title), "%s", meta.title);
            snprintf(track->artist, sizeof(track->artist), "%s", meta.artist);
            snprintf(track->album, sizeof(track->album), "%s", meta.album);
        }
        return;
    }
    if (!art)
        return;
    if (art->title[0])
        snprintf(track->title, sizeof(track->title), "%s", art->title);
    snprintf(track->artist, sizeof(track->artist), "%s", art->artist);
    snprintf(track->album, sizeof(track->album), "%s", art->album);
    if (art->has_cover) {
        track->cover = &art->cover;
        track->thumb = &art->thumb;
    }
}

void uiDrawNoteIcon(int x, int y, int size, Color color)
{
    // Two beamed eighth notes scaled to a size x size box.
    int head = size * 28 / 100, stem = size / 14 > 1 ? size / 14 : 2;
    int left_x = x + size * 12 / 100, right_x = x + size * 56 / 100;
    int head_y = y + size * 70 / 100, top = y + size * 14 / 100;
    gfxFillCircle(left_x + head / 2, head_y + head / 4, head / 2, color);
    gfxFillCircle(right_x + head / 2, head_y + head / 8 - size / 12, head / 2, color);
    gfxFillRect(left_x + head - stem, top + size / 14, stem, head_y - top, color);
    gfxFillRect(right_x + head - stem, top, stem, head_y - top - size / 12, color);
    gfxFillRect(left_x + head - stem, top, right_x - left_x + stem, size / 9, color);
}

void uiDrawCover(int x, int y, int size, const CoverImage *cover)
{
    if (cover && cover->pixels) {
        gfxBlit(x, y, size, cover->size, cover->pixels);
        return;
    }
    gfxFillRoundRect(x, y, size, size, size / 12, COLOR_SURFACE_HI);
    uiDrawNoteIcon(x + size / 4, y + size / 4, size / 2, COLOR_FAINT);
}

#define PILL_TEXT 17
#define PILL_PAD 14

int uiPillWidth(const char *label)
{
    return fontTextWidth(FONT_BOLD, PILL_TEXT, label) + 2 * PILL_PAD;
}

#define ICON_SIZE 20
#define ICON_GAP 7
#define HINT_GAP 9

static void drawArrowHead(float tip_x, float tip_y, float dir_x, float dir_y, Color color)
{
    const float length = 5.5f, half = 4.5f;
    float bx = tip_x - dir_x * length, by = tip_y - dir_y * length;
    gfxTriangle(tip_x, tip_y, bx - dir_y * half, by + dir_x * half, bx + dir_y * half, by - dir_x * half, color);
}

// Two lines crossing left to right, each ending in an arrow.
static void drawShuffleIcon(float x, float y, Color color)
{
    gfxLine(x + 1, y + 5, x + 13, y + 15, 2.2f, color);
    gfxLine(x + 1, y + 15, x + 13, y + 5, 2.2f, color);
    gfxLine(x + 13, y + 5, x + 15, y + 5, 2.2f, color);
    gfxLine(x + 13, y + 15, x + 15, y + 15, 2.2f, color);
    drawArrowHead(x + 20, y + 5, 1, 0, color);
    drawArrowHead(x + 20, y + 15, 1, 0, color);
}

// A circle open at the top right, with an arrow going clockwise.
static void drawRepeatIcon(float x, float y, bool one, Color color)
{
    float cx = x + 10, cy = y + 10, r = one ? 8.0f : 7.0f;
    float start = -1.05f, end = start + 2.0f * (float)M_PI - 0.95f;
    gfxArc(cx, cy, r, 2.2f, start, end, color);
    float ex = cx + r * cosf(end), ey = cy + r * sinf(end);
    drawArrowHead(ex - sinf(end) * 4.0f, ey + cosf(end) * 4.0f, -sinf(end), cosf(end), color);
    if (one)
        fontDrawCentered(FONT_BOLD, 10, cx, cy + 0.5f, "1", color);
}

void uiDrawPlaylistAddIcon(float cx, float cy, float scale, Color color)
{
    float s = scale, t = 1.9f * scale;
    gfxLine(cx - 8 * s, cy - 6 * s, cx + 5 * s, cy - 6 * s, t, color);
    gfxLine(cx - 8 * s, cy - 1 * s, cx + 5 * s, cy - 1 * s, t, color);
    gfxLine(cx - 8 * s, cy + 4 * s, cx - 1 * s, cy + 4 * s, t, color);
    gfxLine(cx + 1.5f * s, cy + 4 * s, cx + 8.5f * s, cy + 4 * s, t, color);
    gfxLine(cx + 5 * s, cy + 0.5f * s, cx + 5 * s, cy + 7.5f * s, t, color);
}

void uiDrawPlaylistRemoveIcon(float cx, float cy, float scale, Color color)
{
    float s = scale, t = 1.9f * scale;
    gfxLine(cx - 8 * s, cy - 6 * s, cx + 5 * s, cy - 6 * s, t, color);
    gfxLine(cx - 8 * s, cy - 1 * s, cx + 5 * s, cy - 1 * s, t, color);
    gfxLine(cx - 8 * s, cy + 4 * s, cx - 1 * s, cy + 4 * s, t, color);
    gfxLine(cx + 1.5f * s, cy + 4 * s, cx + 8.5f * s, cy + 4 * s, t, color);
}

int uiIconPillWidth(const char *hint, const char *label)
{
    int w = 2 * PILL_PAD + ICON_SIZE + ICON_GAP + fontTextWidth(FONT_BOLD, PILL_TEXT, label);
    return hint ? w + uiButtonWidth(hint) + HINT_GAP : w;
}

void uiDrawIconPill(int x, int y, const char *hint, PillIcon icon, const char *label, bool on)
{
    Color bg = on ? gfxMix(COLOR_SURFACE_HI, COLOR_ACCENT, 70) : COLOR_SURFACE_HI;
    Color fg = on ? COLOR_ACCENT : COLOR_DIM;
    gfxFillRoundRect(x, y, uiIconPillWidth(hint, label), 30, 15, bg);
    int cx = x + PILL_PAD;
    float mid = (float)y + 15.0f;
    if (hint) {
        uiDrawButton(cx - 6, mid, hint, COLOR_BG, fg);  // The badge sits close to the pill's edge.
        cx += uiButtonWidth(hint) - 6 + HINT_GAP;
    }
    if (icon == PILL_ICON_SHUFFLE)
        drawShuffleIcon((float)cx, (float)y + 5, fg);
    else if (icon == PILL_ICON_PLAYLIST_ADD)
        uiDrawPlaylistAddIcon((float)cx + ICON_SIZE / 2.0f, mid, 0.9f, fg);
    else
        drawRepeatIcon((float)cx, (float)y + 5, icon == PILL_ICON_REPEAT_ONE, fg);
    fontDrawMiddle(FONT_BOLD, PILL_TEXT, cx + ICON_SIZE + ICON_GAP, mid, label, fg);
}

void uiDrawPill(int x, int y, const char *label, bool on)
{
    int w = uiPillWidth(label);
    gfxFillRoundRect(x, y, w, 30, 15, on ? gfxMix(COLOR_SURFACE_HI, COLOR_ACCENT, 70) : COLOR_SURFACE_HI);
    fontDrawMiddle(FONT_BOLD, PILL_TEXT, x + PILL_PAD, (float)y + 15.0f, label, on ? COLOR_ACCENT : COLOR_DIM);
}

const char *uiRepeatLabel(RepeatMode repeat)
{
    switch (repeat) {
    case REPEAT_ALL: return "Repeat queue";
    case REPEAT_ONE: return "Repeat song";
    default: return "Repeat off";
    }
}
