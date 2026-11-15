// KaiPlayer: a music player for Switch 2 homebrew (kaiHEN + libounce).
// Browse the SD card, pick a folder or a playlist and play its MP3, FLAC,
// Ogg Vorbis and WAV files.
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "app_buttons.h"
#include "artwork.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "player.h"
#include "sysinfo.h"
#include "ui.h"

#define TOAST_NS 3000000000ull
#define SEEK_STEP_SECONDS 10.0
#define PAGE_ROWS UI_LIST_ROWS

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

void appToast(App *app, const char *message)
{
    snprintf(app->toast, sizeof(app->toast), "%s", message);
    app->toast_until_ns = nowNs() + TOAST_NS;
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
    if (pressed & BUTTONS_UP) moveQueueCursor(app, -1, status.track_count);
    if (pressed & BUTTONS_DOWN) moveQueueCursor(app, 1, status.track_count);
    if (pressed & BUTTONS_LEFT) moveQueueCursor(app, -PAGE_ROWS, status.track_count);
    if (pressed & BUTTONS_RIGHT) moveQueueCursor(app, PAGE_ROWS, status.track_count);
    size_t track;
    if ((pressed & HidNpadButton_A) && playerQueueTrack(app->queue_cursor, &track))
        playerJumpTo(track);
    if (pressed & HidNpadButton_B) app->view = VIEW_BROWSER;
    if (pressed & HidNpadButton_X) playerToggleShuffle();
    if (pressed & HidNpadButton_Y) playerCycleRepeat();
}

static void handleNowPlaying(App *app, uint64_t pressed)
{
    if (pressed & HidNpadButton_A) playerTogglePause();
    if (pressed & HidNpadButton_B) app->view = VIEW_BROWSER;
    if (pressed & BUTTONS_LEFT) playerSeek(-SEEK_STEP_SECONDS);
    if (pressed & BUTTONS_RIGHT) playerSeek(SEEK_STEP_SECONDS);
    if (pressed & HidNpadButton_X) playerToggleShuffle();
    if (pressed & HidNpadButton_Y) playerCycleRepeat();
}

// ZL and ZR move between the tabs. Opening the queue shows the current track.
static void switchView(App *app, int delta)
{
    app->view = (View)((app->view + VIEW_COUNT + delta) % VIEW_COUNT);
    if (app->view == VIEW_QUEUE) {
        PlayerStatus status;
        playerGetStatus(&status);
        app->queue_cursor = status.queue_pos;
    } else if (app->view == VIEW_PLAYLISTS && !app->playlists.open) {
        appPlaylistsRefresh(app);
    }
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

static void handleInput(App *app, uint64_t pressed)
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
    case VIEW_BROWSER: appHandleBrowser(app, pressed); break;
    case VIEW_PLAYLISTS: appHandlePlaylists(app, pressed); break;
    case VIEW_QUEUE: handleQueue(app, pressed); break;
    default: handleNowPlaying(app, pressed); break;
    }
    handleGlobal(app, pressed);
}

// Keeps the console awake while music plays, and surfaces player errors.
static void syncWithPlayer(App *app, PlayerState *last_state, char *last_error, size_t error_size)
{
    PlayerStatus status;
    playerGetStatus(&status);
    if (status.state != *last_state) {
        appletSetMediaPlaybackState(status.state == PLAYER_PLAYING);
        *last_state = status.state;
    }
    if (strcmp(status.error, last_error) != 0) {
        snprintf(last_error, error_size, "%s", status.error);
        if (status.error[0])
            appToast(app, status.error);
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

static void openStartFolder(App *app)
{
    if (!browserOpen(&app->browser, app->config.folder) && !browserOpen(&app->browser, "sdmc:/"))
        appToast(app, "Could not read the SD card.");
}

static void saveSettings(App *app)
{
    PlayerStatus status;
    playerGetStatus(&status);
    app->config.shuffle = status.shuffle;
    app->config.repeat = status.repeat;
    configSave(&app->config);
}

static void runLoop(App *app)
{
    PadState pad;
    padInitializeDefault(&pad);
    InputRepeat repeat;
    inputRepeatInit(&repeat, BUTTONS_REPEATING);
    PlayerState last_state = PLAYER_STOPPED;
    char last_error[sizeof(((PlayerStatus *)0)->error)] = "";

    while (!app->quit && appletMainLoop()) {
        padUpdate(&pad);
        uint64_t now = nowNs();
        uint64_t pressed = inputRepeatUpdate(&repeat, padGetButtonsDown(&pad), padGetButtons(&pad), now);
        if (app->audio_ok) {
            handleInput(app, pressed);
            syncWithPlayer(app, &last_state, last_error, sizeof(last_error));
            appSaveSession(app, now, false);
        } else if (pressed & HidNpadButton_Plus) {
            app->quit = true;
        }

        PlayerStatus status;
        playerGetStatus(&status);
        requestArtwork(&status);
        sysinfoGet(&app->sys, now);
        gfxBegin();
        uiDraw(app, &status, now);
        gfxEnd();
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    static App app;
    configLoad(&app.config);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    if (!gfxInit())
        return 1;
    if (!fontInit()) {
        gfxExit();
        return 1;
    }
    artworkInit();
    sysinfoInit();
    app.audio_ok = playerInit(app.config.shuffle, app.config.repeat);
    openStartFolder(&app);
    if (app.audio_ok)
        appRestoreSession(&app);

    runLoop(&app);

    if (app.audio_ok) {
        appSaveSession(&app, nowNs(), true);
        saveSettings(&app);
        appletSetMediaPlaybackState(false);
        playerExit();
    }
    artworkExit();
    sysinfoExit();
    appPlaylistsClose(&app);
    playlistIndexFree(&app.playlists.index);
    trackListFree(&app.pending);
    browserFree(&app.browser);
    fontExit();
    gfxExit();
    return 0;
}
