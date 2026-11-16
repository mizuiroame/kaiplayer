// Starting playback, and resuming the last session.
#include <stdio.h>

#include <switch.h>

#include "app.h"
#include "player.h"
#include "resume.h"

#define SAVE_INTERVAL_NS 30000000000ull  // While playing.

// What was saved last, to save again only when something changed.
static unsigned g_saved_generation;
static size_t g_saved_track = (size_t)-1;
static PlayerState g_saved_state = PLAYER_STOPPED;
static uint64_t g_next_save_ns;

void appPlayTracks(App *app, TrackList *tracks, size_t start, bool shuffle, const char *playlist)
{
    if (tracks->count == 0) {
        appToast(app, "There are no songs to play.");
        return;
    }
    if (shuffle) {
        playerSetShuffle(true);
        start = (size_t)(armGetSystemTick() % tracks->count);
    }
    snprintf(app->playing_playlist, sizeof(app->playing_playlist), "%s", playlist ? playlist : "");
    playerSetMixedAlbums(playlist != NULL);
    playerPlay(tracks, start < tracks->count ? start : 0);  // The view stays where it is.
}

void appRestoreSession(App *app, bool play)
{
    TrackList queue;
    size_t track;
    double position;
    if (!resumeLoad(&queue, &track, &position))
        return;
    playerRestore(&queue, track, position);
    if (play)
        playerTogglePause();  // Restored paused: this resumes it.
    app->view = VIEW_NOW_PLAYING;
}

void appSaveSession(App *app, uint64_t now_ns, bool force)
{
    (void)app;
    PlayerStatus status;
    playerGetStatus(&status);
    if (status.track_count == 0)
        return;
    bool changed = status.generation != g_saved_generation || status.track != g_saved_track ||
                   status.state != g_saved_state;
    bool due = status.state == PLAYER_PLAYING && now_ns >= g_next_save_ns;
    if (!force && !changed && !due)
        return;

    TrackList queue;
    size_t track;
    double position;
    if (playerSnapshot(&queue, &track, &position)) {
        resumeSave(&queue, track, position);
        trackListFree(&queue);
    }
    // Recorded even on failure, so an out-of-memory snapshot isn't retried
    // every frame; the next change or interval tries again.
    g_saved_generation = status.generation;
    g_saved_track = status.track;
    g_saved_state = status.state;
    g_next_save_ns = now_ns + SAVE_INTERVAL_NS;
}
