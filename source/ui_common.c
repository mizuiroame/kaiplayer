#include "ui_common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lang.h"
#include "meta.h"
#include "ui_buttons.h"

Color g_ui_network = COLOR_NETWORK_OWN, g_ui_server = COLOR_SERVER_OWN;
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
        snprintf(track->title, sizeof(track->title), "%s", T("Nothing playing"));
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
    if (art->has_lyrics)
        track->lyrics = &art->lyrics;
}

void uiDrawNoteIcon(int x, int y, int size, Color color)
{
    // Two notes on a beam, with smooth (anti-aliased) edges at any size.
    float s = (float)size, head = s * 0.17f, stem = s * 0.085f;
    float left = x + s * 0.27f, right = x + s * 0.73f, bottom = y + s * 0.78f, top = y + s * 0.16f;
    gfxLine(left + head - stem / 2, bottom - head * 0.2f, left + head - stem / 2, top + s * 0.08f, stem, color);
    gfxLine(right + head - stem / 2, bottom - head * 0.6f, right + head - stem / 2, top, stem, color);
    gfxTriangle(left + head - stem, top + s * 0.08f, right + head, top - s * 0.02f, right + head, top + s * 0.1f, color);
    gfxTriangle(left + head - stem, top + s * 0.08f, right + head, top + s * 0.1f, left + head - stem, top + s * 0.2f,
                color);
    gfxLine(left, bottom - head * 0.2f, left, bottom - head * 0.2f, head * 2.0f, color);  // Note heads.
    gfxLine(right, bottom - head * 0.6f, right, bottom - head * 0.6f, head * 2.0f, color);
}

void uiDrawPersonIcon(float cx, float cy, float size, Color color)
{
    float s = size;
    gfxLine(cx, cy - s * 0.16f, cx, cy - s * 0.16f, s * 0.26f, color);  // Head: a line of no length is a dot.
    gfxLine(cx - s * 0.13f, cy + s * 0.24f, cx + s * 0.13f, cy + s * 0.24f, s * 0.28f, color);  // Shoulders.
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
    else if (icon == PILL_ICON_CHECK) {
        gfxLine((float)cx + 3, mid, (float)cx + 8, mid + 5, 2.4f, fg);
        gfxLine((float)cx + 8, mid + 5, (float)cx + 17, mid - 5, 2.4f, fg);
    } else if (icon == PILL_ICON_LYRICS) {  // A bubble with two lines of words.
        float x0 = (float)cx + 1, top = mid - 8;
        gfxFillRoundRect(cx + 1, (int)top, 18, 13, 3, fg);
        gfxTriangle(x0 + 4, top + 12, x0 + 9, top + 12, x0 + 3, top + 17, fg);
        gfxLine(x0 + 4, top + 4.5f, x0 + 14, top + 4.5f, 1.8f, COLOR_SURFACE_HI);
        gfxLine(x0 + 4, top + 8.5f, x0 + 11, top + 8.5f, 1.8f, COLOR_SURFACE_HI);
    } else if (icon == PILL_ICON_PLAYLIST_ADD)
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
    case REPEAT_ALL: return T("Repeat queue");
    case REPEAT_ONE: return T("Repeat song");
    default: return T("Repeat off");
    }
}
