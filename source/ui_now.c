// The Now Playing view: large cover, tags, progress and transport controls.
#include <stdio.h>
#include <stdlib.h>

#include "lang.h"
#include "ui.h"
#include "ui_common.h"
#include "ui_buttons.h"
#include "ui_hit.h"
#include "nowstyle.h"
#include "ui_nowstyle.h"
#include "util.h"

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
// Lyrics: the line sung in the middle, two before and two after, sliding up as it changes.
#define LYRICS_TOP 206
#define LYRICS_BOTTOM 362
#define LYRICS_LINE_H 32
#define LYRICS_MIDDLE 276
#define LYRICS_SLIDE_MS 260
#define LYRICS_AROUND 3
#define SHADOW_OFFSET 10
#define SHADOW_ALPHA 110
// Around the cover's box: where a record (with its tonearm), CD or cassette
// (with its shadow) is drawn. Kept clear of the song details on the right.
#define STYLE_AREA_X (COVER_X - 24)
#define STYLE_AREA_Y (COVER_Y - 24)
#define STYLE_AREA_W (COVER_SIZE + 64)
#define STYLE_AREA_H (COVER_SIZE + 48)

// What's behind the record or CD while an overlay is open, so it can keep
// turning under the overlay: see uiNowPlayingUnderOverlay.
static uint32_t *g_style_backdrop;
static bool g_style_backdrop_ok;

static void styleAreaFb(int *x, int *y, int *w, int *h)
{
    *x = gfxToFb(STYLE_AREA_X), *y = gfxToFb(STYLE_AREA_Y);
    *w = gfxToFb(STYLE_AREA_X + STYLE_AREA_W) - *x, *h = gfxToFb(STYLE_AREA_Y + STYLE_AREA_H) - *y;
}

static void saveStyleBackdrop(void)
{
    int x, y, w, h;
    styleAreaFb(&x, &y, &w, &h);
    if (!g_style_backdrop)
        g_style_backdrop = malloc((size_t)w * h * sizeof(uint32_t));
    if (g_style_backdrop)
        gfxSaveRectFb(x, y, w, h, g_style_backdrop);
    g_style_backdrop_ok = g_style_backdrop != NULL;
}

static bool drawStyle(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns)
{
    return uiDrawNowStyle(app->config.now_style, COVER_X, COVER_Y, COVER_SIZE, track,
                          status->state == PLAYER_PLAYING, now_ns, status->position, status->duration);
}

void uiNowPlayingUnderOverlay(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns)
{
    if (app->config.now_style == NOW_STYLE_COVER || !g_style_backdrop_ok)
        return;
    int x, y, w, h;
    styleAreaFb(&x, &y, &w, &h);
    gfxDrawImageFb(x, y, w, h, g_style_backdrop);
    drawStyle(app, status, track, now_ns);
    gfxBlendRect(STYLE_AREA_X, STYLE_AREA_Y, STYLE_AREA_W, STYLE_AREA_H, COLOR_SHADOW, UI_DIM_ALPHA);
}

static void drawBackdrop(const UiTrack *track)
{
    if (backgroundPixels())
        return;  // The user's picture shows instead.
    Color tint = track->cover ? track->cover->average : COLOR_ACCENT;
    unsigned strength = track->cover ? BACKDROP_STRENGTH : 40;
    gfxGradientV(0, 0, GFX_WIDTH, CONTENT_BOTTOM, gfxMix(COLOR_BG, tint, strength), COLOR_BG);
}

// The file's format in a quiet pill above the progress bar's right end, over
// the song's length: "FLAC · 24-bit · 96 kHz".
static void drawFormat(const PlayerStatus *status)
{
    char label[48];
    decoderFormatLabel(&status->format, label, sizeof(label));
    if (!label[0])
        return;
    int w = fontTextWidth(FONT_BOLD, 13, label) + 20, x = INFO_X + INFO_W - w;
    gfxFillRoundRect(x, PROGRESS_Y - 36, w, 24, 12, COLOR_SURFACE_HI);
    fontDrawMiddle(FONT_BOLD, 13, x + 10, PROGRESS_Y - 24.0f, label, COLOR_DIM);
}

static void drawInfo(const PlayerStatus *status, const UiTrack *track, const char *playlist)
{
    fontDraw(FONT_BOLD, 15, INFO_X, 150, status->state == PLAYER_PAUSED ? T("PAUSED") : T("NOW PLAYING"), COLOR_DIM);
    fontDrawClipped(FONT_BOLD, 44, INFO_X, 172, INFO_W, track->title, track->has_track ? COLOR_TEXT : COLOR_DIM);
    const char *artist = track->artist[0] ? track->artist : T("Unknown artist");
    if (track->has_track)
        fontDrawClipped(FONT_REGULAR, 26, INFO_X, 236, INFO_W, artist, COLOR_TEXT);
    if (track->album[0])
        fontDrawClipped(FONT_REGULAR, 20, INFO_X, 276, INFO_W, track->album, COLOR_DIM);

    if (status->track_count > 0) {  // With the playlist's name when it came from one.
        char line[PLAYLIST_NAME_MAX + 128];
        int n = snprintf(line, sizeof(line), T("Track %zu of %zu"), status->queue_pos + 1, status->track_count);
        if (playlist[0] && n > 0 && (size_t)n < sizeof(line))
            snprintf(line + n, sizeof(line) - (size_t)n, "  ·  %s", playlist);
        fontDrawClipped(FONT_REGULAR, 20, INFO_X, 314, INFO_W, line, COLOR_DIM);
    }
}

static float easeOut(float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// The title and artist on one line, then the lyrics around the line being sung.
static void drawLyrics(const PlayerStatus *status, const UiTrack *track)
{
    fontDraw(FONT_BOLD, 15, INFO_X, 150, status->state == PLAYER_PAUSED ? T("PAUSED") : T("NOW PLAYING"), COLOR_DIM);
    char head[UTIL_PATH_MAX * 2 + 8];
    snprintf(head, sizeof(head), "%s%s%s", track->title, track->artist[0] ? "  \xC2\xB7  " : "", track->artist);
    fontDrawClipped(FONT_BOLD, 26, INFO_X, 170, INFO_W, head, COLOR_TEXT);

    const Lyrics *l = track->lyrics;
    int at = lyricsLineAt(l, status->position);
    // How far the slide to this line has gone: the lines move up one place as a new one starts.
    float slide = 1.0f;
    if (at >= 0) {
        double since = status->position * 1000.0 - l->lines[at].ms;
        slide = easeOut((float)(since / LYRICS_SLIDE_MS));
    }
    float shift = (1.0f - slide) * LYRICS_LINE_H;
    gfxSetClip(INFO_X, LYRICS_TOP, INFO_W, LYRICS_BOTTOM - LYRICS_TOP);
    for (int i = at - LYRICS_AROUND; i <= at + LYRICS_AROUND; i++) {
        if (i < 0 || i >= l->count)
            continue;
        const char *text = l->lines[i].text[0] ? l->lines[i].text : "\xE2\x80\xA2 \xE2\x80\xA2 \xE2\x80\xA2";  // A pause: three dots.
        float y = LYRICS_MIDDLE + (i - at) * LYRICS_LINE_H + shift;
        bool current = i == at;
        int px = current ? 24 : 20;
        Color color = current ? COLOR_TEXT : gfxMix(COLOR_DIM, COLOR_BG, i < at ? 110 : 40);
        fontDrawClipped(current ? FONT_BOLD : FONT_REGULAR, px, INFO_X, (int)(y - px * 0.7f), INFO_W, text, color);
    }
    if (at < 0) {  // Before the first line: the first ones, waiting.
        for (int i = 0; i < l->count && i < LYRICS_AROUND; i++)
            fontDrawClipped(FONT_REGULAR, 20, INFO_X, (int)(LYRICS_MIDDLE + (i + 1) * LYRICS_LINE_H - 14), INFO_W,
                            l->lines[i].text, gfxMix(COLOR_DIM, COLOR_BG, 40));
    }
    gfxResetClip();
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
    uiDrawButtonCentered((float)cx, (float)y + 11.0f, label, COLOR_SURFACE_HI, COLOR_TEXT);
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

static void drawToggles(const PlayerStatus *status, const UiTrack *track, bool lyrics_on, bool add_focused)
{
    int x = INFO_X;
    const char *shuffle = status->shuffle ? T("Shuffle on") : T("Shuffle off");
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
        const char *const ADD = T("Add to playlist");
        x += repeat_w + 12;
        uiDrawIconPill(x, TOGGLES_Y, "B", PILL_ICON_PLAYLIST_ADD, ADD, add_focused);
        uiHitAdd(x, TOGGLES_Y - 8, uiIconPillWidth("B", ADD), 46, HIT_ADD, -1);
        x += uiIconPillWidth("B", ADD) + 12;
    }
    if (track->lyrics) {  // Only for a song with lyrics.
        const char *const LYRICS = T("Lyrics");
        uiDrawIconPill(x, TOGGLES_Y, "^", PILL_ICON_LYRICS, LYRICS, lyrics_on);
        uiHitAdd(x, TOGGLES_Y - 8, uiIconPillWidth("^", LYRICS), 46, HIT_LYRICS, 0);
    }
}

void uiDrawNowPlaying(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns)
{
    drawBackdrop(track);
    g_style_backdrop_ok = false;
    if (uiOverlayOpen(app) && app->config.now_style != NOW_STYLE_COVER && gfxCanReadBack())
        saveStyleBackdrop();  // This frame is kept under the overlay; the record keeps turning on it.
    if (!drawStyle(app, status, track, now_ns)) {
        if (track->cover)  // The placeholder has round corners and no shadow.
            gfxBlendRect(COVER_X + SHADOW_OFFSET, COVER_Y + SHADOW_OFFSET, COVER_SIZE, COVER_SIZE,
                         COLOR_SHADOW, SHADOW_ALPHA);
        uiDrawCover(COVER_X, COVER_Y, COVER_SIZE, track->cover);
    }
    if (track->lyrics && app->config.show_lyrics)
        drawLyrics(status, track);
    else
        drawInfo(status, track, app->playing_playlist);
    drawProgress(app, status);
    drawFormat(status);
    drawTransport(status);
    drawToggles(status, track, app->config.show_lyrics, app->focus == FOCUS_ADD);
}
