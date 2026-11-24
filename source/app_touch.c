// Touch input: taps act like the matching button, and drags on a list move
// it with the finger (and it glides on, as on a phone). Targets come from the regions the UI drew last frame.
#include <switch.h>

#include "app.h"
#include "app_buttons.h"
#include "dav.h"
#include "player.h"
#include "subsonic.h"
#include "ui_hit.h"
#include "ui_list.h"

#define SCREEN_W 1280
#define SCREEN_H 720
#define PANEL_W 1920  // In case the touch panel reports its own resolution.
#define SEEK_MARGIN 12  // Matches the slack ui_now.c gives the progress bar.

static bool g_touch_ready;
static bool g_full_hd;  // Seen coordinates beyond 1280x720: scale them down.

static bool overlayOpen(const App *app)
{
    return app->keyboard_open || app->menu.open || app->settings_open || app->eq_open || app->about_open || app->picker_open ||
           app->welcome_open;
}

// The gain at height y on a slider: the top is +12 dB, the bottom -12 dB.
static int sliderGain(const HitRegion *r, int y)
{
    double t = (double)(y - r->y) / (double)(r->h > 1 ? r->h - 1 : 1);
    t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    double gain = EQ_GAIN_MAX - t * (EQ_GAIN_MAX - EQ_GAIN_MIN);
    return (int)(gain < 0 ? gain - 0.5 : gain + 0.5);
}

// "+" on a row (or, with index -1, in Now Playing) adds that song or folder.
static void addFromRow(App *app, int index)
{
    if (index < 0) {
        appAddToPlaylist(app, SRC_NOW_PLAYING);
        return;
    }
    appActivateRow(app, (size_t)index, true);
    switch (app->view) {
    case VIEW_BROWSER:
        appAddToPlaylist(app, appLibraryPage(app)->level == LIB_FOLDERS ? SRC_BROWSE_ENTRY : SRC_LIBRARY_ENTRY);
        break;
    case VIEW_QUEUE: appAddToPlaylist(app, SRC_QUEUE_ENTRY); break;
    case VIEW_PLAYLISTS:
        if (app->playlists.open)
            appConfirmRemoveSong(app);
        break;
    default: break;
    }
}

static double seekFraction(const HitRegion *r, int x)
{
    // The region has SEEK_MARGIN of slack on each side of the bar.
    double fraction = (double)(x - r->x - SEEK_MARGIN) / (double)(r->w - 2 * SEEK_MARGIN);
    return fraction < 0.0 ? 0.0 : fraction > 1.0 ? 1.0 : fraction;
}

static void tapRegion(App *app, const HitRegion *r, int x, int y)
{
    // Targets are from the last frame. If an overlay opened or closed since,
    // they belong to the other layer: ignore the tap.
    if (r->kind == HIT_NOTICE) {  // Over every layer.
        noticeDismiss(&app->notices, (uint64_t)r->arg, armTicksToNs(armGetSystemTick()));
        return;
    }
    if (uiHitIsOverlayTarget(r->kind) != overlayOpen(app))
        return;
    PlayerStatus status;
    playerGetStatus(&status);
    app->focus = FOCUS_LIST;  // Touch moves away from button focus.
    switch (r->kind) {
    case HIT_NOTICE: break;  // Sent away above.
    case HIT_TAB: appSetView(app, (View)r->arg); break;
    case HIT_MENU_BUTTON:
        if (!overlayOpen(app) && !app->playlists.moving)
            appOpenMenu(app);
        break;
    case HIT_ROW: appActivateRow(app, (size_t)r->arg, false); break;
    case HIT_MINI_PLAYER: appSetView(app, VIEW_NOW_PLAYING); break;
    case HIT_SEEK:
        if (status.duration > 0)
            playerSeekTo(status.duration * seekFraction(r, x));
        break;
    case HIT_PREV: playerPrev(); break;
    case HIT_PLAY: playerTogglePause(); break;
    case HIT_NEXT: playerNext(); break;
    case HIT_SHUFFLE: playerToggleShuffle(); break;
    case HIT_REPEAT: playerCycleRepeat(); break;
    case HIT_LYRICS: appToggleLyrics(app); break;
    case HIT_BACKDROP:
        // Outside a menu or Settings closes it; the keyboard keeps its text, and the welcome waits for an answer.
        if (!app->keyboard_open && !app->welcome_open)
            appDispatchButtons(app, HidNpadButton_B);
        break;
    case HIT_MENU_ITEM:
        if (!app->menu.open || r->arg >= app->menu.count)
            break;
        app->menu.cursor = r->arg;
        appDispatchButtons(app, HidNpadButton_A);
        break;
    case HIT_KEY:
        app->keyboard.row = r->arg / 16;
        app->keyboard.col = r->arg % 16;
        appDispatchButtons(app, HidNpadButton_A);
        break;
    case HIT_SETTING:
        app->settings_cursor = (SettingRow)r->arg;
        appActivateSetting(app, (SettingRow)r->arg);
        break;
    case HIT_BACK:
    case HIT_OVERLAY_BACK: appDispatchButtons(app, HidNpadButton_B); break;
    case HIT_EQ_BAND: appEqualizerSetGain(app, r->arg, sliderGain(r, y)); break;
    case HIT_EQ_PRESET: appEqualizerNextPreset(app); break;
    case HIT_EQ_TOGGLE: appEqualizerToggle(app); break;
    case HIT_PICKER_ROW:
        if ((size_t)r->arg < appPickerFolderCount(app)) {
            app->picker.cursor = (size_t)r->arg;
            appHandleFolderPicker(app, HidNpadButton_A);
        }
        break;
    case HIT_PICKER_USE: appPickerChoose(app); break;
    case HIT_WELCOME_ROW:
        if (app->welcome_open && r->arg >= 0 && r->arg < WELCOME_ROW_COUNT)
            appWelcomeChoose(app, (WelcomeRow)r->arg);
        break;
    case HIT_CARD:
        app->home_row = r->arg / HOME_ROW_MAX;
        app->home_col[app->home_row] = (size_t)(r->arg % HOME_ROW_MAX);
        app->focus = FOCUS_LIST;
        appDispatchButtons(app, HidNpadButton_A);
        break;
    case HIT_ADD: addFromRow(app, r->arg); break;
    case HIT_SOURCE: appSwitchLibrarySource(app); break;
    case HIT_NETWORK_SCAN: appRemoteScanTapped(app, r->arg == 1 ? SUB_ROOT : DAV_ROOT); break;
    case HIT_LIST:
    case HIT_PANEL: break;
    }
}

// Moves the list in view by px as a finger does, and its cursor with the
// same steps as Up and Down to stay in view. False at an end.
static bool scrollList(App *app, int px)
{
    bool moved = uiListScrollPx(px);
    size_t want = g_list_scroll.want_cursor;
    for (; g_list_scroll.cursor < want; g_list_scroll.cursor++)
        appDispatchButtons(app, HidNpadButton_Down);
    for (; g_list_scroll.cursor > want; g_list_scroll.cursor--)
        appDispatchButtons(app, HidNpadButton_Up);
    return moved;
}

// A drag that starts on a list moves it with the finger, as on a phone.
static void dragList(App *app, const TouchEvent *e, uint64_t now_ns)
{
    const HitRegion *start = uiHitFind(e->start_x, e->start_y);
    // Lists scroll; the folder picker's goes by rows, over its panel.
    bool list = start && (start->kind == HIT_LIST || start->kind == HIT_ROW) && !overlayOpen(app);
    bool picker = start && (start->kind == HIT_PICKER_ROW || start->kind == HIT_PANEL) && app->picker_open &&
                  !app->menu.open && !app->keyboard_open;
    if (!(list || picker) || app->playlists.moving)
        return;
    bool starting = !app->kinetic.dragging;
    kineticDrag(&app->kinetic, starting ? 0 : -e->dy, now_ns);
    if (picker) {  // Not one of the lists that follow the finger: a row a step.
        static int rows;
        rows = starting ? -e->dy : rows - e->dy;
        for (; rows >= ROW_H; rows -= ROW_H)
            appDispatchButtons(app, HidNpadButton_Down);
        for (; rows <= -ROW_H; rows += ROW_H)
            appDispatchButtons(app, HidNpadButton_Up);
        return;
    }
    scrollList(app, -e->dy);  // Finger up: the content goes up, showing what's below.
}

// A finger came down, let go, or nothing: the list glides on, or stops.
static void glide(App *app, TouchEvent *e, uint64_t now_ns)
{
    if (e->type == TOUCH_PRESS) {  // A press stops a glide, and isn't a tap on what it stopped over.
        app->swallow_tap = kineticGliding(&app->kinetic);
        kineticStop(&app->kinetic);
    } else if (e->type == TOUCH_TAP && app->swallow_tap) {
        e->type = TOUCH_NONE;
    }
    if (e->type != TOUCH_PRESS && e->type != TOUCH_DRAG)
        app->swallow_tap = false;
    if (e->type == TOUCH_DRAG_END && app->kinetic.dragging)
        kineticRelease(&app->kinetic, now_ns);
    if (overlayOpen(app) || app->playlists.moving)
        kineticStop(&app->kinetic);
    int px = kineticStep(&app->kinetic, now_ns);
    if (px && !scrollList(app, px))
        kineticStop(&app->kinetic);  // An end.
}

// A drag that starts on the progress bar scrubs: the bar follows the finger,
// and the song jumps there on release. Other drags scroll lists.
static void dragSeekOrList(App *app, const TouchEvent *e, uint64_t now_ns)
{
    const HitRegion *start = uiHitFind(e->start_x, e->start_y);
    if (start && start->kind == HIT_EQ_BAND && app->eq_open) {  // Drag a slider.
        appEqualizerSetGain(app, start->arg, sliderGain(start, e->y));
        return;
    }
    bool on_bar = start && start->kind == HIT_SEEK && !overlayOpen(app);
    if (!on_bar) {
        app->scrubbing = false;
        if (e->type == TOUCH_DRAG)
            dragList(app, e, now_ns);
        return;
    }
    app->scrub_fraction = seekFraction(start, e->x);
    app->scrubbing = e->type == TOUCH_DRAG;
    if (e->type == TOUCH_DRAG_END) {
        PlayerStatus status;
        playerGetStatus(&status);
        if (status.duration > 0)
            playerSeekTo(status.duration * app->scrub_fraction);
    }
}

static void initTouch(void)
{
    if (!g_touch_ready) {
        hidInitializeTouchScreen();
        g_touch_ready = true;
    }
}

bool appTouchDown(void)
{
    initTouch();
    HidTouchScreenState state = {0};
    return hidGetTouchScreenStates(&state, 1) > 0 && state.count > 0;
}

void appHandleTouch(App *app, uint64_t now_ns)
{
    initTouch();
    HidTouchScreenState state = {0};
    bool touching = hidGetTouchScreenStates(&state, 1) > 0 && state.count > 0;
    if (app->touch_wakes) {
        // The touch that turned the screen on does nothing else.
        if (!touching)
            app->touch_wakes = false;
        touchReset(&app->touch);
        return;
    }
    int x = 0, y = 0;
    if (touching) {
        x = (int)state.touches[0].x;
        y = (int)state.touches[0].y;
        if (x >= SCREEN_W || y >= SCREEN_H)
            g_full_hd = true;
        if (g_full_hd) {
            x = x * SCREEN_W / PANEL_W;
            y = y * SCREEN_W / PANEL_W;
        }
    }
    TouchEvent e = touchUpdate(&app->touch, touching, x, y, now_ns);
    glide(app, &e, now_ns);
    if (e.type == TOUCH_TAP) {
        const HitRegion *r = uiHitFind(e.x, e.y);
        if (r)
            tapRegion(app, r, e.x, e.y);
    } else if (e.type == TOUCH_DRAG || e.type == TOUCH_DRAG_END) {
        dragSeekOrList(app, &e, now_ns);
    }
}
