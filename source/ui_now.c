// The Now Playing view: large cover, tags, progress and transport controls.
#include <stdio.h>

#include "ui.h"
#include "ui_common.h"

#define COVER_SIZE ARTWORK_LARGE_SIZE
#define COVER_X 96
#define COVER_Y 140
#define INFO_X 560
#define INFO_W 640
#define PROGRESS_Y 384
#define TRANSPORT_Y 476
#define TOGGLES_Y 564
#define BACKDROP_STRENGTH 110
#define SHADOW_OFFSET 10
#define SHADOW_ALPHA 110

static void drawBackdrop(const UiTrack *track)
{
    Color tint = track->cover ? track->cover->average : COLOR_ACCENT;
    unsigned strength = track->cover ? BACKDROP_STRENGTH : 40;
    gfxGradientV(0, 0, GFX_WIDTH, CONTENT_BOTTOM, gfxMix(COLOR_BG, tint, strength), COLOR_BG);
}

static void drawInfo(const PlayerStatus *status, const UiTrack *track)
{
    fontDraw(FONT_BOLD, 15, INFO_X, 150, status->state == PLAYER_PAUSED ? "PAUSED" : "NOW PLAYING", COLOR_DIM);
    fontDrawClipped(FONT_BOLD, 44, INFO_X, 172, INFO_W, track->title, track->has_track ? COLOR_TEXT : COLOR_DIM);
    const char *artist = track->artist[0] ? track->artist : "Unknown artist";
    if (track->has_track)
        fontDrawClipped(FONT_REGULAR, 26, INFO_X, 236, INFO_W, artist, COLOR_TEXT);
    if (track->album[0])
        fontDrawClipped(FONT_REGULAR, 20, INFO_X, 276, INFO_W, track->album, COLOR_DIM);

    if (status->track_count > 0) {
        char line[UTIL_PATH_MAX + 48];
        snprintf(line, sizeof(line), "Track %zu of %zu  ·  %s", status->track + 1, status->track_count, track->folder);
        fontDrawClipped(FONT_REGULAR, 16, INFO_X, 312, INFO_W, line, COLOR_FAINT);
    }
}

static void drawProgress(const PlayerStatus *status)
{
    double fraction = status->duration > 0 ? status->position / status->duration : 0.0;
    if (fraction > 1.0)
        fraction = 1.0;
    int filled = (int)(INFO_W * fraction);
    gfxFillRoundRect(INFO_X, PROGRESS_Y, INFO_W, 6, 3, COLOR_SURFACE_HI);
    if (filled > 0)
        gfxFillRoundRect(INFO_X, PROGRESS_Y, filled, 6, 3, COLOR_ACCENT);
    if (status->duration > 0)
        gfxFillCircle(INFO_X + filled, PROGRESS_Y + 3, 8, COLOR_TEXT);

    char elapsed[16], total[16];
    utilFormatTime(elapsed, sizeof(elapsed), status->position);
    utilFormatTime(total, sizeof(total), status->duration);
    const char *right = status->duration > 0 ? total : "--:--";
    fontDraw(FONT_REGULAR, 17, INFO_X, PROGRESS_Y + 16, elapsed, COLOR_DIM);
    fontDraw(FONT_REGULAR, 17, INFO_X + INFO_W - fontTextWidth(FONT_REGULAR, 17, right), PROGRESS_Y + 16,
             right, COLOR_DIM);
}

static void drawSkipIcon(int cx, int cy, bool next, Color color)
{
    if (next) {
        gfxFillTriangleRight(cx - 14, cy - 13, 22, 26, color);
        gfxFillRect(cx + 9, cy - 13, 5, 26, color);
    } else {
        gfxFillTriangleLeft(cx - 8, cy - 13, 22, 26, color);
        gfxFillRect(cx - 14, cy - 13, 5, 26, color);
    }
}

static void drawHint(int cx, int y, const char *label)
{
    fontDraw(FONT_BOLD, 14, cx - fontTextWidth(FONT_BOLD, 14, label) / 2, y, label, COLOR_FAINT);
}

static void drawTransport(const PlayerStatus *status)
{
    int cx = INFO_X + INFO_W / 2, cy = TRANSPORT_Y;
    drawSkipIcon(cx - 120, cy, false, COLOR_TEXT);
    drawSkipIcon(cx + 120, cy, true, COLOR_TEXT);
    gfxFillCircle(cx, cy, 38, COLOR_ACCENT);
    if (status->state == PLAYER_PLAYING) {
        gfxFillRoundRect(cx - 12, cy - 15, 8, 30, 2, COLOR_BG);
        gfxFillRoundRect(cx + 4, cy - 15, 8, 30, 2, COLOR_BG);
    } else {
        gfxFillTriangleRight(cx - 9, cy - 16, 26, 32, COLOR_BG);
    }
    drawHint(cx - 120, cy + 26, "L");
    drawHint(cx, cy + 46, "A");
    drawHint(cx + 120, cy + 26, "R");
}

static int drawPill(int x, int y, const char *label, bool on)
{
    int w = fontTextWidth(FONT_BOLD, 16, label) + 28;
    gfxFillRoundRect(x, y, w, 34, 17, on ? gfxMix(COLOR_SURFACE_HI, COLOR_ACCENT, 70) : COLOR_SURFACE_HI);
    fontDraw(FONT_BOLD, 16, x + 14, y + 7, label, on ? COLOR_ACCENT : COLOR_DIM);
    return w;
}

static void drawToggles(const PlayerStatus *status)
{
    int x = INFO_X;
    x += drawPill(x, TOGGLES_Y, status->shuffle ? "X  Shuffle on" : "X  Shuffle off", status->shuffle) + 12;
    char repeat[32];
    snprintf(repeat, sizeof(repeat), "Y  %s", uiRepeatLabel(status->repeat));
    drawPill(x, TOGGLES_Y, repeat, status->repeat != REPEAT_OFF);

    int bar_w = 160, bar_x = INFO_X + INFO_W - bar_w;
    // KaiPlayer's own volume. The console's volume buttons set the system
    // volume, which an app cannot read, so they don't move this bar.
    char volume[32];
    snprintf(volume, sizeof(volume), "App volume  %d%%", (int)(status->volume * 100.0f + 0.5f));
    int volume_w = fontTextWidth(FONT_REGULAR, 16, volume);
    fontDraw(FONT_REGULAR, 16, bar_x - 14 - volume_w, TOGGLES_Y + 8, volume, COLOR_DIM);
    gfxFillRoundRect(bar_x, TOGGLES_Y + 14, bar_w, 6, 3, COLOR_SURFACE_HI);
    gfxFillRoundRect(bar_x, TOGGLES_Y + 14, (int)(bar_w * status->volume), 6, 3, COLOR_TEXT);
}

void uiDrawNowPlaying(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns)
{
    drawBackdrop(track);
    if (track->cover)  // The placeholder has round corners and no shadow.
        gfxBlendRect(COVER_X + SHADOW_OFFSET, COVER_Y + SHADOW_OFFSET, COVER_SIZE, COVER_SIZE,
                     COLOR_SHADOW, SHADOW_ALPHA);
    uiDrawCover(COVER_X, COVER_Y, COVER_SIZE, track->cover);
    drawInfo(status, track);
    drawProgress(status);
    drawTransport(status);
    drawToggles(status);
    if (app->toast[0] && now_ns < app->toast_until_ns)
        fontDrawClipped(FONT_REGULAR, 17, INFO_X, TOGGLES_Y + 52, INFO_W, app->toast, COLOR_ERROR);
}
