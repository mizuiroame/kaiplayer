#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_common.h"
#include "ui_hit.h"
#include "ui_buttons.h"
#include "ui_list.h"
#include "screen.h"
#include "covercache.h"
#include "theme.h"

#define MINI_Y 572
#define MINI_H 92
#define THUMB_SIZE ARTWORK_SMALL_SIZE
#define TAB_GAP 28
#define BATTERY_LOW 15
#define COLOR_CHARGING GFX_RGB(0x5B, 0xD6, 0x7A)
#define NS_PER_MINUTE 60000000000ull

static const char *const VIEW_NAMES[VIEW_COUNT] = {"Home", "Library", "Playlists", "Queue", "Now Playing"};

static void drawBrowser(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    const Browser *b = &app->browser;
    int path_x = MARGIN, path_w = GFX_WIDTH - 2 * MARGIN;
    {  // Back: up a folder, or to the Library at the music folder.
        int back_w = uiDrawBackButton(MARGIN - 4, SUBTITLE_Y - 6, app->focus == FOCUS_BACK);
        path_x += back_w + 12;
        path_w -= back_w + 12;
    }
    bool warned = b->incomplete;
    if (warned) {
        // The console returned an error while listing: some names may be missing.
        static const char WARNING[] = "Some items in this folder could not be read";
        int warning_w = fontTextWidth(FONT_REGULAR, TEXT_SMALL, WARNING);
        fontDraw(FONT_REGULAR, TEXT_SMALL, GFX_WIDTH - MARGIN - warning_w, SUBTITLE_Y, WARNING, COLOR_ERROR);
        path_w -= warning_w + 24;
    }
    bool has_songs = false;
    for (size_t i = 0; i < b->count && !has_songs; i++)
        has_songs = !b->entries[i].is_dir;
    if (has_songs && !warned) {
        uiDrawColumnHeaders();
        path_w = COLUMN_ARTIST_X - 24 - path_x;
    }
    fontDrawClipped(FONT_REGULAR, TEXT_SMALL, path_x, SUBTITLE_Y, path_w, b->path, COLOR_DIM);
    if (b->count == 0) {
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10, "This folder has no subfolders or music.", COLOR_DIM);
        return;
    }
    char playing[UTIL_PATH_MAX] = "", path[UTIL_PATH_MAX];
    if (status->state != PLAYER_STOPPED)
        playerTrackPath(status->track, playing, sizeof(playing));
    size_t first = uiFirstVisible(b->cursor, b->count);
    uiHitListArea();
    for (size_t i = first; i < b->count && i < first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H;
        const BrowserEntry *e = &b->entries[i];
        uiHitRow(y, i);
        if (i == b->cursor)
            uiDrawSelection(y, COLOR_ACCENT);
        bool is_song = !e->is_dir && utilJoinPath(path, sizeof(path), b->path, e->name);
        bool current = is_song && playing[0] && strcmp(path, playing) == 0;
        if (e->is_dir)
            uiDrawFolderIcon(MARGIN + 4, y + 13, COLOR_FOLDER);
        else if (current)
            uiDrawPlayingBars(MARGIN + 6, y + 12, status->state == PLAYER_PLAYING, now_ns);
        else
            uiDrawNoteIcon(MARGIN + 4, y + 10, 24, COLOR_ACCENT);
        static UiSong song;  // Big; the UI draws on one thread.
        if (is_song)
            uiLoadSong(path, &song);
        int name_w = is_song ? COLUMN_ARTIST_X - 24 - (MARGIN + 44) : GFX_WIDTH - 2 * MARGIN - 60 - ADD_BUTTON_W;
        FontFace face = i == b->cursor || current ? FONT_BOLD : FONT_REGULAR;
        if (is_song)
            uiDrawSongTitle(&song, face, MARGIN + 44, y + 8, name_w, current ? COLOR_ACCENT : COLOR_TEXT);
        else
            fontDrawClipped(face, TEXT_ROW, MARGIN + 44, y + 8, name_w, e->name, COLOR_FOLDER);
        if (is_song)
            uiDrawSongColumns(&song, y);
        uiDrawAddButton(GFX_WIDTH - MARGIN - 16 - ADD_BUTTON_W + 8, y, i, i == b->cursor && app->focus == FOCUS_ADD);
    }
    uiDrawScrollbar(first, b->count);
}

// One row of the queue: row.index is a position in the play order.
static void drawQueueRow(TrackRow *row)
{
    char path[UTIL_PATH_MAX];
    size_t track;
    if (playerQueueTrack(row->index, &track) && playerTrackPath(track, path, sizeof(path))) {
        row->path = path;
        uiDrawTrackRow(row);
    }
}

static void drawQueue(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    if (status->track_count == 0) {
        uiDrawEmptyList("Queue is empty", "Pick a folder in Browse or a playlist, and press X to play it.");
        return;
    }
    // The rows follow the play order, so a shuffled queue shows its real order.
    char line[96];
    snprintf(line, sizeof(line), "%zu tracks  ·  %s", status->track_count,
             status->shuffle ? "shuffled play order" : "in folder order");
    fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y, line, COLOR_DIM);
    uiDrawColumnHeaders();

    size_t cursor = app->queue_cursor < status->track_count ? app->queue_cursor : 0;
    size_t first = uiFirstVisible(cursor, status->track_count);
    uiHitListArea();
    for (size_t i = first; i < status->track_count && i < first + UI_LIST_ROWS; i++) {
        bool current = i == status->queue_pos && status->state != PLAYER_STOPPED;
        uiHitRow(LIST_Y + (int)(i - first) * ROW_H, i);
        TrackRow row = {
            .number = i + 1, .y = LIST_Y + (int)(i - first) * ROW_H, .index = i,
            .selected = i == cursor, .current = current, .playing = status->state == PLAYER_PLAYING,
            .add_focused = i == cursor && app->focus == FOCUS_ADD, .now_ns = now_ns,
        };
        drawQueueRow(&row);
    }
    uiDrawScrollbar(first, status->track_count);
}

#define TAB_TEXT_MIDDLE 28.5f  // Middle of the tab names' capitals.
#define HINT_GAP 14

// The tabs, centered, between the ZL and ZR hints.
static void drawTabs(const App *app)
{
    int widths[VIEW_COUNT], total = 0;
    for (int i = 0; i < VIEW_COUNT; i++) {
        widths[i] = fontTextWidth((int)app->view == i ? FONT_BOLD : FONT_REGULAR, 20, VIEW_NAMES[i]);
        total += widths[i] + (i > 0 ? TAB_GAP : 0);
    }
    int x = (GFX_WIDTH - total) / 2;
    uiDrawButton(x - HINT_GAP - uiButtonWidth("ZL"), TAB_TEXT_MIDDLE, "ZL", COLOR_SURFACE_HI, COLOR_DIM);
    for (int i = 0; i < VIEW_COUNT; i++) {
        bool active = (int)app->view == i;
        fontDraw(active ? FONT_BOLD : FONT_REGULAR, 20, x, 18, VIEW_NAMES[i], active ? COLOR_TEXT : COLOR_DIM);
        uiHitAdd(x - TAB_GAP / 2, 4, widths[i] + TAB_GAP, HEADER_H - 8, HIT_TAB, i);
        if (active)
            gfxFillRoundRect(x, 48, widths[i], 3, 1, COLOR_ACCENT);
        x += widths[i] + TAB_GAP;
    }
    uiDrawButton(x - TAB_GAP + HINT_GAP, TAB_TEXT_MIDDLE, "ZR", COLOR_SURFACE_HI, COLOR_DIM);
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
    if (charging) {  // A lightning bolt over the battery.
        float cx = (float)x + 15.0f, cy = (float)y + 8.0f;
        // Two halves that overlap in the middle, so no seam shows between them.
        gfxTriangle(cx + 2.0f, cy - 8.0f, cx - 5.0f, cy + 1.5f, cx + 1.5f, cy + 1.5f, COLOR_BG);
        gfxTriangle(cx - 2.0f, cy + 8.0f, cx + 5.0f, cy - 1.5f, cx - 1.5f, cy - 1.5f, COLOR_BG);
        gfxTriangle(cx + 1.5f, cy - 6.0f, cx - 3.5f, cy + 1.0f, cx + 1.0f, cy + 1.0f, COLOR_TEXT);
        gfxTriangle(cx - 1.5f, cy + 6.0f, cx + 3.5f, cy - 1.0f, cx - 1.0f, cy - 1.0f, COLOR_TEXT);
    }
}

// Clock, battery and the sleep timer in the top right corner.
static void drawStatus(const App *app, uint64_t now_ns)
{
    const SysInfo *sys = &app->sys;
    int x = GFX_WIDTH - MARGIN;
    if (sys->has_battery) {
        char percent[8];
        snprintf(percent, sizeof(percent), "%d%%", sys->battery_percent);
        x -= 34;
        drawBattery(x, 20, sys->battery_percent, sys->charging);  // Centered on the text.
        x -= fontTextWidth(FONT_REGULAR, 17, percent) + 8;
        fontDraw(FONT_REGULAR, 17, x, 19, percent, sys->charging ? COLOR_CHARGING : COLOR_DIM);
        x -= 20;
    }
    if (sys->has_time) {
        char clock[16];
        utilFormatClock(clock, sizeof(clock), sys->hour, sys->minute, app->config.clock_24h);
        x -= fontTextWidth(FONT_BOLD, 18, clock);
        fontDraw(FONT_BOLD, 18, x, 18, clock, COLOR_TEXT);
        x -= 20;
    }
    if (app->sleep_deadline_ns > now_ns) {
        char sleep[32];
        unsigned minutes = (unsigned)((app->sleep_deadline_ns - now_ns + NS_PER_MINUTE - 1) / NS_PER_MINUTE);
        snprintf(sleep, sizeof(sleep), "Sleep %u min", minutes);
        x -= fontTextWidth(FONT_BOLD, 16, sleep);
        fontDraw(FONT_BOLD, 16, x, 20, sleep, COLOR_ACCENT);
    }
}

// Three bars before the title: the touch way into the + menu.
static void drawMenuButton(void)
{
    for (int i = 0; i < 3; i++)
        gfxFillRoundRect(MARGIN, 22 + i * 7, 22, 3, 1, COLOR_ACCENT);
}

static void drawHeader(const App *app, uint64_t now_ns)
{
    drawMenuButton();
    int title_w = fontDraw(FONT_BOLD, 26, MARGIN + 36, 16, "KaiPlayer", COLOR_ACCENT);
    uiHitAdd(0, 0, MARGIN + 36 + title_w + 12, HEADER_H, HIT_MENU_BUTTON, 0);
    drawTabs(app);
    drawStatus(app, now_ns);
}

#define BAR_CONTROL_GAP 76
#define BAR_PREV_X (GFX_WIDTH / 2 - BAR_CONTROL_GAP)  // Play in the middle of the screen.
#define BAR_CY (MINI_Y + 46)
#define FOCUS_RING 3

static void drawBarSkip(int cx, int cy, bool next, Color color)
{
    if (next) {
        gfxFillTriangleRight(cx - 10, cy - 10, 16, 20, color);
        gfxFillRect(cx + 6, cy - 10, 4, 20, color);
    } else {
        gfxFillTriangleLeft(cx - 6, cy - 10, 16, 20, color);
        gfxFillRect(cx - 10, cy - 10, 4, 20, color);
    }
}

// A ring around the control that has the button focus.
static void drawFocusRing(int x, int y, int w, int h, int radius)
{
    gfxFillRoundRect(x - FOCUS_RING, y - FOCUS_RING, w + 2 * FOCUS_RING, h + 2 * FOCUS_RING, radius + FOCUS_RING,
                     COLOR_TEXT);
    gfxFillRoundRect(x, y, w, h, radius, COLOR_SURFACE);
}

static void drawBarTransport(const PlayerStatus *status, int focused)
{
    int prev_x = BAR_PREV_X, play_x = prev_x + BAR_CONTROL_GAP, next_x = play_x + BAR_CONTROL_GAP;
    if (focused == BAR_PREV || focused == BAR_NEXT)
        drawFocusRing((focused == BAR_PREV ? prev_x : next_x) - 26, BAR_CY - 26, 52, 52, 26);
    if (focused == BAR_PLAY)
        drawFocusRing(play_x - 30, BAR_CY - 30, 60, 60, 30);
    drawBarSkip(prev_x, BAR_CY, false, COLOR_TEXT);
    drawBarSkip(next_x, BAR_CY, true, COLOR_TEXT);
    gfxFillCircle(play_x, BAR_CY, 26, COLOR_ACCENT);
    if (status->state == PLAYER_PLAYING) {
        gfxFillRoundRect(play_x - 9, BAR_CY - 11, 6, 22, 2, COLOR_BG);
        gfxFillRoundRect(play_x + 3, BAR_CY - 11, 6, 22, 2, COLOR_BG);
    } else {
        gfxFillTriangleRight(play_x - 7, BAR_CY - 12, 20, 24, COLOR_BG);
    }
    uiHitAdd(prev_x - 36, MINI_Y + 6, 72, MINI_H - 6, HIT_PREV, 0);
    uiHitAdd(play_x - 36, MINI_Y + 6, 72, MINI_H - 6, HIT_PLAY, 0);
    uiHitAdd(next_x - 36, MINI_Y + 6, 72, MINI_H - 6, HIT_NEXT, 0);
}

static void drawMiniPlayer(const App *app, const PlayerStatus *status, const UiTrack *track, uint64_t now_ns)
{
    int focused = app->focus == FOCUS_PLAYER ? (int)app->bar_control : -1;
    gfxFillRect(0, MINI_Y, GFX_WIDTH, MINI_H, COLOR_SURFACE);
    gfxFillRect(0, MINI_Y, GFX_WIDTH, 3, COLOR_FAINT);
    uiHitAdd(0, MINI_Y, GFX_WIDTH, MINI_H, HIT_MINI_PLAYER, 0);
    if (status->duration > 0) {
        double fraction = status->position / status->duration;
        gfxFillRect(0, MINI_Y, (int)(GFX_WIDTH * (fraction > 1.0 ? 1.0 : fraction)), 3, COLOR_ACCENT);
    }

    uiDrawCover(MARGIN - 8, MINI_Y + 10, THUMB_SIZE, track->thumb);
    int text_x = MARGIN + THUMB_SIZE + 12;
    int text_w = BAR_PREV_X - 48 - text_x;
    fontDrawClipped(FONT_BOLD, 24, text_x, MINI_Y + 14, text_w, track->title,
                    track->has_track ? COLOR_TEXT : COLOR_DIM);

    bool toast = app->toast[0] && now_ns < app->toast_until_ns;
    const char *second = toast ? app->toast : track->artist[0] ? track->artist : track->folder;
    fontDrawClipped(FONT_REGULAR, 20, text_x, MINI_Y + 50, text_w, second, toast ? COLOR_ERROR : COLOR_DIM);
    drawBarTransport(status, focused);

    char elapsed[16], total[16], line[96];
    utilFormatTime(elapsed, sizeof(elapsed), status->position);
    utilFormatTime(total, sizeof(total), status->duration);
    snprintf(line, sizeof(line), "%s%s / %s", status->state == PLAYER_PAUSED ? "Paused  " : "",
             elapsed, status->duration > 0 ? total : "--:--");
    fontDraw(FONT_REGULAR, 20, GFX_WIDTH - MARGIN - fontTextWidth(FONT_REGULAR, 20, line),
             MINI_Y + 14, line, COLOR_TEXT);

    // Shuffle and repeat as pills, lit in the accent color when on.
    const char *shuffle = status->shuffle ? "Shuffle on" : "Shuffle off";
    const char *repeat = uiRepeatLabel(status->repeat);
    PillIcon repeat_icon = status->repeat == REPEAT_ONE ? PILL_ICON_REPEAT_ONE : PILL_ICON_REPEAT;
    int repeat_w = uiIconPillWidth(NULL, repeat), shuffle_w = uiIconPillWidth(NULL, shuffle);
    int x = GFX_WIDTH - MARGIN - repeat_w;
    if (focused == BAR_REPEAT)
        drawFocusRing(x, MINI_Y + 48, repeat_w, 30, 15);
    uiDrawIconPill(x, MINI_Y + 48, NULL, repeat_icon, repeat, status->repeat != REPEAT_OFF);
    uiHitAdd(x, MINI_Y + 40, repeat_w, 46, HIT_REPEAT, 0);
    x -= shuffle_w + 10;
    if (focused == BAR_SHUFFLE)
        drawFocusRing(x, MINI_Y + 48, shuffle_w, 30, 15);
    uiDrawIconPill(x, MINI_Y + 48, NULL, PILL_ICON_SHUFFLE, shuffle, status->shuffle);
    uiHitAdd(x, MINI_Y + 40, shuffle_w, 46, HIT_SHUFFLE, 0);
}

static const char *footerHelp(const App *app)
{
    static const char *const HELP[VIEW_COUNT] = {
        "[A] open  [X] play  [Y] shuffle  [LS] move  [RS] seek  [L][R] track  [-] pause  [+] menu",
        "[A] open  [B] back  [>] add  [X] play  [Y] shuffle  [RS] page / seek  [L][R] track  [+] menu",
        "[A] open  [X] play  [Y] shuffle  [RS] page / seek  [L][R] track  [-] pause  [+] menu",
        "[A] play  [B] library  [>] add  [X] shuffle  [Y] repeat  [RS] page / seek  [L][R] track  [+] menu",
        "[A] pause  [B] add to playlist  [<>][RS] seek  [X] shuffle  [Y] repeat  [L][R] track  [+] menu",
    };
    if (app->keyboard_open)
        return "[A] type  [B] delete  [X] shift  [Y] space  [+] done  [-] cancel";
    if (app->about_open)
        return "[A][B] back";
    if (app->picker_open)
        return app->picker_first_run ? "[A] open  [B] up  [X] use this folder"
                                     : "[A] open  [B] up or close  [X] use this folder";
    if (app->eq_open)
        return "[<>] band  [^v] gain  [X] preset  [Y] on/off  [B] back";
    if (app->settings_open)
        return "[^v] choose  [<>][A] change  [B] close";
    if (app->menu.open && app->menu.kind == MENU_KIND_EXIT)
        return "[A] choose  [B] cancel  [+] exit";
    if (app->menu.open)
        return app->menu.kind == MENU_KIND_MAIN ? "[A] choose  [B] close  [+] exit..." : "[A] choose  [B] close";
    if (app->playlists.moving)
        return "[^v] move the song  [A][B] done";
    if (app->focus == FOCUS_PLAYER && app->view != VIEW_NOW_PLAYING)
        return "[<>] choose  [A] press  [^][B] back to the list  [RS] seek  [L][R] track  [-] pause";
    if (app->view == VIEW_BROWSER) {
        switch (appLibraryPage((App *)app)->level) {
        case LIB_ROOT: return "[A] open  [LS] move  [RS] seek  [L][R] track  [-] pause  [+] menu";
        case LIB_ARTISTS:
        case LIB_ALBUMS: return "[A] open  [B] back  [>] add  [X] play  [Y] shuffle  [RS] page / seek  [L][R] track  [+] menu";
        case LIB_ARTIST: return "[A] open album  [B] back  [>] add  [X] play artist  [Y] shuffle  [RS] page / seek  [L][R] track";
        case LIB_ALBUM:
        case LIB_SONGS: return "[A] play  [B] back  [>] add  [X] play all  [Y] shuffle  [RS] page / seek  [L][R] track  [+] menu";
        default: break;
        }
    }
    if (app->view == VIEW_PLAYLISTS && app->playlists.open)
        return "[A] play  [B] back  [>] remove  [X] play  [Y] shuffle  [RS] page / seek  [L][R] track  [+] menu";
    return HELP[app->view];
}

static void drawFooter(const App *app)
{
    uiDrawHelp(MARGIN, FOOTER_Y + 26.0f, GFX_WIDTH - 2 * MARGIN, footerHelp(app));
}

void uiDrawScreenOff(void)
{
    gfxClear(GFX_RGB(0, 0, 0));
    // Only seen when the backlight can't be turned off: keep it very dim.
    if (!screenHasBacklightControl())
        fontDraw(FONT_REGULAR, 16, MARGIN, GFX_HEIGHT - 40, "Screen off  ·  press any button or touch to wake",
                 GFX_RGB(0x30, 0x30, 0x36));
}

// What's behind an open overlay, dimmed, and what it showed: drawn again
// only when one of these changes (a setting, the song), not every frame.
typedef struct {
    Config config;
    size_t track;
    PlayerState state;
    int view;
    const void *cover;  // The art arrives after the song changes.
} Frozen;

static uint32_t *g_frozen;
static Frozen g_frozen_key;
static bool g_frozen_ok;

static bool frozenStill(const App *app, const PlayerStatus *status, const UiTrack *track, Frozen *key)
{
    memset(key, 0, sizeof(*key));
    key->config = app->config;
    key->track = status->track;
    key->state = status->state;
    key->view = app->view;
    key->cover = track->cover;
    return g_frozen_ok && memcmp(key, &g_frozen_key, sizeof(*key)) == 0;
}

void uiDraw(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    UiTrack track;
    uiHitReset();
    const Artwork *art = artworkLock();
    coverCacheLock();
    g_ui_list_focused = app->focus != FOCUS_PLAYER;
    uiResolveTrack(status, art, &track);
    g_ui_accent = themeAccentColor(app->config.accent, track.cover ? track.cover->vibrant : 0, track.cover != NULL);

    // Only the CPU keeps the view behind an overlay: the GPU draws it all again quickly.
    bool overlay = uiOverlayOpen(app), keep = overlay && gfxCanReadBack();
    Frozen key;
    if (keep && frozenStill(app, status, &track, &key)) {
        gfxCopyFrame(g_frozen);
        if (app->view == VIEW_NOW_PLAYING)
            uiNowPlayingUnderOverlay(app, status, &track, now_ns);
        uiDrawOverlay(app, now_ns);
        drawFooter(app);
        coverCacheUnlock();
        artworkUnlock();
        return;
    }
    const uint32_t *background = backgroundPixels();
    if (background)
        gfxCopyFrame(background);
    else
        gfxClear(COLOR_BG);
    if (app->view == VIEW_NOW_PLAYING) {
        uiDrawNowPlaying(app, status, &track, now_ns);
    } else {
        if (!app->audio_ok)
            fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y,
                     "Audio unavailable: audout could not start.", COLOR_ERROR);
        else if (app->view == VIEW_HOME)
            uiDrawHome(app, status, now_ns);
        else if (app->view == VIEW_BROWSER && appLibraryPage((App *)app)->level == LIB_FOLDERS)
            drawBrowser(app, status, now_ns);
        else if (app->view == VIEW_BROWSER)
            uiDrawLibrary(app, status, now_ns);
        else if (app->view == VIEW_PLAYLISTS)
            uiDrawPlaylists(app, status, now_ns);
        else
            drawQueue(app, status, now_ns);
        drawMiniPlayer(app, status, &track, now_ns);
    }
    drawHeader(app, now_ns);
    g_frozen_ok = false;
    if (overlay)
        gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, COLOR_SHADOW, UI_DIM_ALPHA);
    if (keep) {
        if (!g_frozen)
            g_frozen = malloc((size_t)GFX_FB_WIDTH * GFX_FB_HEIGHT * sizeof(uint32_t));
        if (g_frozen) {
            gfxSaveFrame(g_frozen);
            g_frozen_key = key;
            g_frozen_ok = true;
        }
    }
    uiDrawOverlay(app, now_ns);
    drawFooter(app);  // Over the dimmed screen, so the overlay's help reads well.
    coverCacheUnlock();
    artworkUnlock();
}
