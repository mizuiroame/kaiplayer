// KaiPlayer: a music player for Switch 2 homebrew (kaiHEN + libounce).
// Browse the SD card, pick a folder or a playlist and play its MP3, FLAC,
// Ogg Vorbis, WAV and DSD files.
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "approot.h"
#include "app_buttons.h"
#include "ui_list.h"
#include "artwork.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "lang.h"
#include "meta.h"
#include "catalog_scan.h"
#include "covercache.h"
#include "dav.h"
#include "player.h"
#include "resume.h"
#include "screen.h"
#include "subsonic.h"
#include "sysinfo.h"
#include "ui.h"

#define SEEK_STEP_SECONDS 10.0
#define PAGE_ROWS UI_LIST_ROWS

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

void appNotify(App *app, NoticeKind kind, const char *message)
{
    noticePush(&app->notices, kind, message, nowNs());
}

void appToast(App *app, const char *message)
{
    appNotify(app, NOTICE_INFO, message);
}

const char *appLastNotice(const App *app)
{
    return app->notices.count ? app->notices.items[app->notices.count - 1].text : "";
}

static void moveQueueCursor(App *app, long delta, size_t count)
{
    if (count == 0)
        return;
    long next = (long)app->queue_cursor + delta;
    if (next < 0) next = 0;
    if (next >= (long)count) next = (long)count - 1;
    app->queue_cursor = (size_t)next;
}

static void handleQueue(App *app, uint64_t pressed)
{
    PlayerStatus status;
    playerGetStatus(&status);
    pressed = appFocusFilter(app, pressed, app->queue_cursor, status.track_count, false, status.track_count > 0);
    if (pressed & BUTTONS_UP) moveQueueCursor(app, -1, status.track_count);
    if (pressed & BUTTONS_DOWN) moveQueueCursor(app, 1, status.track_count);
    if (pressed & BUTTONS_PAGE_UP) moveQueueCursor(app, -PAGE_ROWS, status.track_count);
    if (pressed & BUTTONS_PAGE_DOWN) moveQueueCursor(app, PAGE_ROWS, status.track_count);
    size_t track;
    if (pressed & BUTTON_ROW_ACTION)
        appAddToPlaylist(app, SRC_QUEUE_ENTRY);
    else if ((pressed & HidNpadButton_A) && playerQueueTrack(app->queue_cursor, &track))
        playerJumpTo(track);
    if (pressed & HidNpadButton_B) app->view = VIEW_BROWSER;
    if (pressed & HidNpadButton_X) playerToggleShuffle();
    if (pressed & HidNpadButton_Y) playerCycleRepeat();
}

static void handleNowPlaying(App *app, uint64_t pressed)
{
    // B adds the song to a playlist (or tap "Add to playlist").
    if (pressed & HidNpadButton_B)
        appAddToPlaylist(app, SRC_NOW_PLAYING);
    if (pressed & HidNpadButton_A)
        playerTogglePause();
    // The d-pad seeks here; the right stick does too, everywhere. The left
    // stick does nothing, rather than repeat them.
    if (pressed & HidNpadButton_Left) playerSeek(-SEEK_STEP_SECONDS);
    if (pressed & HidNpadButton_Right) playerSeek(SEEK_STEP_SECONDS);
    if (pressed & HidNpadButton_X) playerToggleShuffle();
    if (pressed & HidNpadButton_Y) playerCycleRepeat();
}

// Opening the queue shows the current track; Playlists reloads its list.
void appSetView(App *app, View view)
{
    app->view = view;
    app->focus = FOCUS_LIST;
    if (app->view == VIEW_QUEUE) {
        PlayerStatus status;
        playerGetStatus(&status);
        app->queue_cursor = status.queue_pos;
    } else if (app->view == VIEW_PLAYLISTS && !app->playlists.open) {
        appPlaylistsRefresh(app);
    }
}

// ZL and ZR move between the tabs.
static void switchView(App *app, int delta)
{
    appSetView(app, (View)((app->view + VIEW_COUNT + delta) % VIEW_COUNT));
}

void appActivateRow(App *app, size_t index, bool select_only)
{
    PlayerStatus status;
    playerGetStatus(&status);
    PlaylistsTab *t = &app->playlists;
    // The index comes from the last frame; a list that shrank since makes it stale.
    switch (app->view) {
    case VIEW_BROWSER:
        if (appLibraryPage(app)->level == LIB_FOLDERS) {
            if (index >= app->browser.count)
                return;
            app->browser.cursor = index;
        } else {
            const Catalog *c = catalogLock();
            size_t count = appLibraryCount(app, c);
            catalogUnlock();
            if (index >= count)
                return;
            appLibraryPage(app)->cursor = index;
        }
        break;
    case VIEW_PLAYLISTS:
        if (t->moving || index >= (t->open ? t->tracks.count : t->index.count))
            return;
        if (t->open)
            t->track_cursor = index;
        else
            t->cursor = index;
        break;
    case VIEW_QUEUE:
        if (index >= status.track_count)
            return;
        app->queue_cursor = index;
        break;
    default:
        return;
    }
    if (!select_only)
        appDispatchButtons(app, HidNpadButton_A);
}

static void handleGlobal(App *app, uint64_t pressed)
{
    if (pressed & HidNpadButton_ZL) switchView(app, -1);
    if (pressed & HidNpadButton_ZR) switchView(app, 1);
    if (pressed & HidNpadButton_Minus) playerTogglePause();
    if (pressed & HidNpadButton_L) playerPrev();
    if (pressed & HidNpadButton_R) playerNext();
    if (pressed & HidNpadButton_StickRLeft) playerSeek(-SEEK_STEP_SECONDS);
    if (pressed & HidNpadButton_StickRRight) playerSeek(SEEK_STEP_SECONDS);
}

void appDispatchButtons(App *app, uint64_t pressed)
{
    if (appHandleOverlay(app, pressed))
        return;
    if (app->playlists.moving) {  // Moving a song takes all the buttons, + too.
        appHandlePlaylists(app, pressed);
        return;
    }
    if (pressed & HidNpadButton_Plus) {
        appOpenMenu(app);
        return;
    }
    switch (app->view) {
    case VIEW_HOME: appHandleHome(app, pressed); break;
    case VIEW_BROWSER: appHandleLibrary(app, pressed); break;
    case VIEW_PLAYLISTS: appHandlePlaylists(app, pressed); break;
    case VIEW_QUEUE: handleQueue(app, pressed); break;
    default: handleNowPlaying(app, pressed); break;
    }
    handleGlobal(app, pressed);
}

// Keeps the console awake while music plays, counts plays for Home, and
// surfaces player errors.
static void syncWithPlayer(App *app, PlayerState *last_state, char *last_error, size_t error_size)
{
    PlayerStatus status;
    playerGetStatus(&status);
    appTrackPlays(app, &status);
    appServerUpdate(app, &status);  // Its test's answer, and the song playing told to the server.
    if (status.state != *last_state) {
        appletSetMediaPlaybackState(status.state == PLAYER_PLAYING);
        *last_state = status.state;
    }
    if (strcmp(status.error, last_error) != 0) {
        snprintf(last_error, error_size, "%s", status.error);
        if (status.error[0])
            appNotify(app, NOTICE_ERROR, status.error);
    }
}

// Loads the tags and cover of the playing track in the background.
static void requestArtwork(const PlayerStatus *status)
{
    char path[UTIL_PATH_MAX] = "";
    if (status->track_count > 0)
        playerTrackPath(status->track, path, sizeof(path));
    artworkRequest(path);
}

// Opens the last folder if it is inside the music folder, else the music
// folder; with no music folder yet, asks for one. The network folder's is
// listed when the Folders page opens: KaiPlayer doesn't start by waiting for its server.
static void openStartFolder(App *app)
{
    const char *root = appMusicRoot(app);
    if (appLibraryIsRemote(app))
        return;
    bool inside = utilPathWithin(app->config.folder, root);
    if (!(inside && browserOpen(&app->browser, app->config.folder)) && !browserOpen(&app->browser, root) &&
        !browserOpen(&app->browser, "sdmc:/"))
        appNotify(app, NOTICE_ERROR, T("Could not read the SD card."));
    if (!app->config.library[0])
        appOpenFolderPicker(app, true);
}

static void saveSettings(App *app)
{
    PlayerStatus status;
    playerGetStatus(&status);
    app->config.shuffle = status.shuffle;
    app->config.repeat = status.repeat;
    configSave(&app->config);
}

#define NS_PER_MINUTE 60000000000ull

static bool overlayOpen(const App *app)
{
    return app->keyboard_open || app->menu.open || app->settings_open || app->eq_open || app->about_open || app->picker_open;
}

// Screen off: any button or touch brings it back, and that input does
// nothing else. While music plays and nothing is touched, it turns off after
// the minutes chosen in Settings. Returns true while the screen is off.
static bool manageScreen(App *app, uint64_t pressed, uint64_t now)
{
    bool input = pressed != 0 || appTouchDown();
    if (app->screen_off) {
        if (input) {
            screenSetOn(true);
            app->screen_off = false;
            app->touch_wakes = appTouchDown();
            app->last_input_ns = now;
        }
        return true;
    }
    if (input || !app->last_input_ns || overlayOpen(app))
        app->last_input_ns = now;
    PlayerStatus status;
    playerGetStatus(&status);
    uint64_t idle_limit = (uint64_t)app->config.screen_off_minutes * NS_PER_MINUTE;
    if (idle_limit && status.state == PLAYER_PLAYING && !overlayOpen(app) && now - app->last_input_ns >= idle_limit) {
        appScreenOff(app);
        return true;
    }
    return false;
}

static void runLoop(App *app)
{
    PadState pad;
    padInitializeDefault(&pad);
    InputRepeat repeat;
    inputRepeatInit(&repeat, BUTTONS_REPEATING);
    PlayerState last_state = PLAYER_STOPPED;
    char last_error[sizeof(((PlayerStatus *)0)->error)] = "";
    bool waking = false;

    while (!app->quit && appletMainLoop()) {
        padUpdate(&pad);
        uint64_t now = nowNs();
        uint64_t held = padGetButtons(&pad);
        uint64_t pressed = inputRepeatUpdate(&repeat, padGetButtonsDown(&pad), held, now);
        bool was_off = app->screen_off;
        bool screen_off = app->audio_ok && manageScreen(app, pressed, now);
        if (was_off && !app->screen_off)
            waking = true;
        // The press that wakes the screen does nothing else: buttons only
        // act again once every button has been let go.
        app->buttons_down = padGetButtonsDown(&pad);
        if (waking) {
            waking = (held & ~BUTTONS_STICKS) != 0;
            pressed = 0;
        }
        if (screen_off) {
            appTickSleepTimer(app, now);
            syncWithPlayer(app, &last_state, last_error, sizeof(last_error));
            if (app->config.resume != RESUME_OFF)
                appSaveSession(app, now, false);
        } else if (app->audio_ok) {
            appNetworkUpdate(app);
            if (pressed & (BUTTONS_UP | BUTTONS_DOWN | BUTTONS_PAGE_UP | BUTTONS_PAGE_DOWN)) {
                uiListRelease();  // The buttons take over from the finger.
                kineticStop(&app->kinetic);
            }
            appDispatchButtons(app, pressed);
            appHandleTouch(app, now);
            appTickSleepTimer(app, now);
            syncWithPlayer(app, &last_state, last_error, sizeof(last_error));
            if (app->config.resume != RESUME_OFF)
                appSaveSession(app, now, false);
        } else if (pressed & HidNpadButton_Plus) {
            app->quit = true;
        }

        appUpdatesTick(app);
        app->notice = noticeShown(&app->notices, now, &app->notice_slide);
        PlayerStatus status;
        playerGetStatus(&status);
        requestArtwork(&status);
        sysinfoGet(&app->sys, now);
        gfxBegin();
        if (app->screen_off)
            uiDrawScreenOff();
        else
            uiDraw(app, &status, now);
        gfxEnd();
    }
}

// Shows why KaiPlayer can't start, on libnx's text console, until + is
// pressed, and writes it to error.txt in KaiPlayer's folder.
static void startupError(const char *message)
{
    char path[UTIL_PATH_MAX];
    snprintf(path, sizeof(path), "%s/error.txt", configFolder());
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", message);
        fclose(f);
    }
    consoleInit(NULL);
    printf("\n  %s\n\n  Details are in %s.\n  Press + to exit.\n", message, path);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}

int main(int argc, char **argv)
{
    // KaiPlayer's files go next to its NRO; the first time, what the old
    // folder had (settings, history, resume, catalog) is copied there.
    char folder[APPROOT_PATH_MAX];
    bool next_to_nro = appRootFolder(argc > 0 ? argv[0] : NULL, "kaiplayer", folder, sizeof(folder));
    appRootMakeFolder(folder);
    if (next_to_nro)
        appRootMigrate("sdmc:/config/kaiplayer", folder);
    configSetFolder(folder);
    resumeSetDir(folder);

    static App app;
    configLoad(&app.config);
    appStartLanguages(&app);  // Before anything is said on screen.
    appNetworkStart(&app);    // Before the library: it may be the network folder's,
    appServerStart(&app);     // or the server's.
    appStartUpdates(&app, argc > 0 ? argv[0] : NULL);
    appUsePlaylistFolder(&app);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    if (!gfxInit()) {
        startupError("KaiPlayer could not start drawing (the GPU or its window).");
        return 1;
    }
    if (!fontInit()) {
        gfxExit();
        startupError("KaiPlayer could not load its fonts.");
        return 1;
    }
    artworkInit();
    metaInit();
    catalogScanInit();
    coverCacheInit();
    appLoadHistory(&app);
    if (app.config.library[0] || appLibraryIsRemote(&app))
        catalogScanStart(appMusicRoot(&app));
    // The others on the network in the background, after the one shown: quick when nothing changed.
    if (appNetworkReady() && appLibrarySource(&app) != LIBRARY_NETWORK)
        catalogScanRefreshRemote(DAV_ROOT, false);
    if (appServerReady() && appLibrarySource(&app) != LIBRARY_SERVER)
        catalogScanRefreshRemote(SUB_ROOT, false);
    sysinfoInit();
    screenInit();
    app.audio_ok = playerInit(app.config.shuffle, app.config.repeat);
    if (app.audio_ok)
        appApplyAudioSettings(&app);
    openStartFolder(&app);
    if (app.audio_ok && app.config.resume != RESUME_OFF)
        appRestoreSession(&app, app.config.resume == RESUME_PLAYING);

    runLoop(&app);

    if (app.audio_ok) {
        if (app.config.resume != RESUME_OFF)
            appSaveSession(&app, nowNs(), true);
        saveSettings(&app);
        appletSetMediaPlaybackState(false);
        playerExit();
    }
    coverCacheExit();
    catalogScanExit();
    historyFree(&app.history);
    metaExit();
    artworkExit();
    appStopUpdates();  // A download stops; its part is removed.
    appServerExit();   // After everything that read the server's files,
    appNetworkExit();  // and the network folder's.
    sysinfoExit();
    screenExit();
    appPlaylistsClose(&app);
    playlistIndexFree(&app.playlists.index);
    trackListFree(&app.pending);
    browserFree(&app.browser);
    fontExit();
    gfxExit();
    return 0;
}
