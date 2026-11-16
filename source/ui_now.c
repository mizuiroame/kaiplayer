// The Now Playing view: large cover, tags, progress and transport controls.
#include <stdio.h>

#include "ui.h"
#include "ui_common.h"
#include "ui_hit.h"

#define COVER_SIZE ARTWORK_LARGE_SIZE
#define COVER_X 96
#define COVER_Y 140
#define INFO_X 560
#define INFO_W 640
#define PROGRESS_Y 384
#define PROGRESS_REST_ALPHA 70
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
        snprintf(line, sizeof(line), "Track %zu of %zu  ·  %s", status->queue_pos + 1, status->track_count,
                 track->folder);
        fontDrawClipped(FONT_REGULAR, 20, INFO_X, 314, INFO_W, line, COLOR_DIM);
    }
}

static void drawProgress(const App *app, const PlayerStatus *status)
{
    // While the finger drags the bar, show where it will jump to.
    double position = app->scrubbing ? app->scrub_fraction * status->duration : status->position;
    double fraction = status->duration > 0 ? position / status->duration : 0.0;
    if (fraction > 1.0)
        fraction = 1.0;
    int filled = (int)(INFO_W * fraction);
    // The rest of the song: translucent white, so it shows over any cover tint.
    gfxBlendRect(INFO_X, PROGRESS_Y + 1, INFO_W, 4, COLOR_TEXT, PROGRESS_REST_ALPHA);
    uiHitAdd(INFO_X - 12, PROGRESS_Y - 18, INFO_W + 24, 42, HIT_SEEK, 0);  // 12 px slack: app_touch.c
    if (filled > 0)
        gfxFillRoundRect(INFO_X, PROGRESS_Y, filled, 6, 3, COLOR_ACCENT);
    if (status->duration > 0)
        gfxFillCircle(INFO_X + filled, PROGRESS_Y + 3, 8, COLOR_TEXT);

    char elapsed[16], total[16];
    utilFormatTime(elapsed, sizeof(elapsed), position);
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
    fontDraw(FONT_BOLD, 17, cx - fontTextWidth(FONT_BOLD, 17, label) / 2, y, label, COLOR_DIM);
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
    uiHitAdd(cx - 160, cy - 40, 80, 80, HIT_PREV, 0);
    uiHitAdd(cx - 44, cy - 44, 88, 88, HIT_PLAY, 0);
    uiHitAdd(cx + 80, cy - 40, 80, 80, HIT_NEXT, 0);
    drawHint(cx - 120, cy + 26, "L");
    drawHint(cx, cy + 46, "A");
    drawHint(cx + 120, cy + 26, "R");
}

static void drawToggles(const PlayerStatus *status, bool add_focused)
{
    int x = INFO_X;
    const char *shuffle = status->shuffle ? "Shuffle on" : "Shuffle off";
    int shuffle_w = uiIconPillWidth("X", shuffle);
    uiDrawIconPill(x, TOGGLES_Y, "X", PILL_ICON_SHUFFLE, shuffle, status->shuffle);
    uiHitAdd(x, TOGGLES_Y - 8, shuffle_w, 46, HIT_SHUFFLE, 0);
    x += shuffle_w + 12;
    const char *repeat = uiRepeatLabel(status->repeat);
    int repeat_w = uiIconPillWidth("Y", repeat);
    uiDrawIconPill(x, TOGGLES_Y, "Y", status->repeat == REPEAT_ONE ? PILL_ICON_REPEAT_ONE : PILL_ICON_REPEAT, repeat,
                   status->repeat != REPEAT_OFF);
    uiHitAdd(x, TOGGLES_Y - 8, repeat_w, 46, HIT_REPEAT, 0);
    if (status->track_count > 0) {  // Add the current song to a playlist.
        static const char ADD[] = "+  Add to playlist";
        x += repeat_w + 12;
        uiDrawPill(x, TOGGLES_Y, ADD, add_focused);
        uiHitAdd(x, TOGGLES_Y - 8, uiPillWidth(ADD), 46, HIT_ADD, -1);
    }
}

void uiDrawNowPlaying(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns)
{
    drawBackdrop(track);
    if (track->cover)  // The placeholder has round corners and no shadow.
        gfxBlendRect(COVER_X + SHADOW_OFFSET, COVER_Y + SHADOW_OFFSET, COVER_SIZE, COVER_SIZE,
                     COLOR_SHADOW, SHADOW_ALPHA);
    uiDrawCover(COVER_X, COVER_Y, COVER_SIZE, track->cover);
    drawInfo(status, track);
    drawProgress(app, status);
    drawTransport(status);
    drawToggles(status, app->focus == FOCUS_ADD);
    if (app->toast[0] && now_ns < app->toast_until_ns)
        fontDrawClipped(FONT_REGULAR, 17, INFO_X, TOGGLES_Y + 52, INFO_W, app->toast, COLOR_ERROR);
}
