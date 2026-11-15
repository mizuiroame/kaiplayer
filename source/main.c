// KaiPlayer: a music player for Switch 2 homebrew (kaiHEN + libounce).
// Browse the SD card, pick a folder and play its MP3, FLAC, Ogg Vorbis and
// WAV files.
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "gfx.h"
#include "input.h"
#include "library.h"
#include "player.h"
#include "ui.h"

#define TOAST_NS 3000000000ull
#define VOLUME_STEP 0.05f
#define SEEK_STEP_SECONDS 10.0
#define PAGE_ROWS UI_LIST_ROWS

#define BUTTONS_UP (HidNpadButton_Up | HidNpadButton_StickLUp)
#define BUTTONS_DOWN (HidNpadButton_Down | HidNpadButton_StickLDown)
#define BUTTONS_LEFT (HidNpadButton_Left | HidNpadButton_StickLLeft)
#define BUTTONS_RIGHT (HidNpadButton_Right | HidNpadButton_StickLRight)
#define BUTTONS_REPEATING (BUTTONS_UP | BUTTONS_DOWN | BUTTONS_LEFT | BUTTONS_RIGHT | \
                           HidNpadButton_ZL | HidNpadButton_ZR | \
                           HidNpadButton_StickRLeft | HidNpadButton_StickRRight)

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

void appToast(App *app, const char *message)
{
    snprintf(app->toast, sizeof(app->toast), "%s", message);
    app->toast_until_ns = nowNs() + TOAST_NS;
}

// Scans folder and starts playing it, from start_name if given.
static void playFolder(App *app, const char *folder, bool recursive, const char *start_name)
{
    TrackList list;
    if (!libraryScan(&list, folder, recursive)) {
        appToast(app, "Out of memory for the track list.");
        return;
    }
    if (list.count == 0) {
        trackListFree(&list);
        appToast(app, "No music in this folder.");
        return;
    }
    size_t start = 0;
    char path[UTIL_PATH_MAX];
    if (start_name && utilJoinPath(path, sizeof(path), folder, start_name)) {
        size_t found = trackListFind(&list, path);
        start = found < list.count ? found : 0;
    }
    playerPlay(&list, start);
    snprintf(app->config.folder, sizeof(app->config.folder), "%s", folder);
    configSave(&app->config);
}

static void openSelected(App *app)
{
    const BrowserEntry *e = browserSelected(&app->browser);
    if (!e)
        return;
    if (!e->is_dir) {
        playFolder(app, app->browser.path, false, e->name);
        return;
    }
    char path[UTIL_PATH_MAX];
    if (!utilJoinPath(path, sizeof(path), app->browser.path, e->name) ||
        !browserOpen(&app->browser, path))
        appToast(app, "Could not open the folder.");
}

static void handleBrowser(App *app, uint64_t pressed)
{
    Browser *b = &app->browser;
    if (pressed & BUTTONS_UP) browserMove(b, -1);
    if (pressed & BUTTONS_DOWN) browserMove(b, 1);
    if (pressed & BUTTONS_LEFT) browserMove(b, -PAGE_ROWS);
    if (pressed & BUTTONS_RIGHT) browserMove(b, PAGE_ROWS);
    if (pressed & HidNpadButton_A) openSelected(app);
    if (pressed & HidNpadButton_B) browserGoUp(b);
    if (pressed & HidNpadButton_X) playFolder(app, b->path, true, NULL);
    if (pressed & HidNpadButton_Y) {
        PlayerStatus status;
        playerGetStatus(&status);
        app->queue_cursor = status.track;
        app->view = VIEW_QUEUE;
    }
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
    if (pressed & HidNpadButton_A) playerJumpTo(app->queue_cursor);
    if (pressed & HidNpadButton_B) app->view = VIEW_BROWSER;
    if (pressed & HidNpadButton_X) playerToggleShuffle();
    if (pressed & HidNpadButton_Y) playerCycleRepeat();
}

static void handleGlobal(uint64_t pressed)
{
    PlayerStatus status;
    playerGetStatus(&status);
    if (pressed & HidNpadButton_Minus) playerTogglePause();
    if (pressed & HidNpadButton_L) playerPrev();
    if (pressed & HidNpadButton_R) playerNext();
    if (pressed & HidNpadButton_ZL) playerSetVolume(status.volume - VOLUME_STEP);
    if (pressed & HidNpadButton_ZR) playerSetVolume(status.volume + VOLUME_STEP);
    if (pressed & HidNpadButton_StickRLeft) playerSeek(-SEEK_STEP_SECONDS);
    if (pressed & HidNpadButton_StickRRight) playerSeek(SEEK_STEP_SECONDS);
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

static void openStartFolder(App *app)
{
    if (!browserOpen(&app->browser, app->config.folder) && !browserOpen(&app->browser, "sdmc:/"))
        appToast(app, "Could not read the SD card.");
}

static void saveSettings(App *app)
{
    PlayerStatus status;
    playerGetStatus(&status);
    app->config.volume = status.volume;
    app->config.shuffle = status.shuffle;
    app->config.repeat = status.repeat;
    configSave(&app->config);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    static App app;
    configLoad(&app.config);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    InputRepeat repeat;
    inputRepeatInit(&repeat, BUTTONS_REPEATING);

    if (!gfxInit())
        return 1;
    app.audio_ok = playerInit(app.config.volume, app.config.shuffle, app.config.repeat);
    openStartFolder(&app);

    PlayerState last_state = PLAYER_STOPPED;
    char last_error[sizeof(((PlayerStatus *)0)->error)] = "";
    while (appletMainLoop()) {
        padUpdate(&pad);
        uint64_t now = nowNs();
        uint64_t pressed = inputRepeatUpdate(&repeat, padGetButtonsDown(&pad), padGetButtons(&pad), now);
        if (pressed & HidNpadButton_Plus)
            break;

        if (app.audio_ok) {
            if (app.view == VIEW_BROWSER)
                handleBrowser(&app, pressed);
            else
                handleQueue(&app, pressed);
            handleGlobal(pressed);
            syncWithPlayer(&app, &last_state, last_error, sizeof(last_error));
        }

        PlayerStatus status;
        playerGetStatus(&status);
        gfxBegin();
        uiDraw(&app, &status, now);
        gfxEnd();
    }

    if (app.audio_ok) {
        saveSettings(&app);
        appletSetMediaPlaybackState(false);
        playerExit();
    }
    browserFree(&app.browser);
    gfxExit();
    return 0;
}
