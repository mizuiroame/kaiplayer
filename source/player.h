// Playback engine: a thread decodes the current track, resamples it to the
// output rate and feeds audout. The UI calls these functions from the main
// thread; they only set state, so they return at once.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "library.h"
#include "queue.h"

typedef enum {
    PLAYER_STOPPED,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} PlayerState;

typedef struct {
    PlayerState state;
    size_t track;         // Index in the track list.
    size_t track_count;
    double position;      // Seconds.
    double duration;      // Seconds, 0 if unknown.
    float volume;
    bool shuffle;
    RepeatMode repeat;
    char error[160];
} PlayerStatus;

bool playerInit(float volume, bool shuffle, RepeatMode repeat);
void playerExit(void);

// Plays list from track start. The player takes the list's contents.
void playerPlay(TrackList *list, size_t start);

void playerTogglePause(void);
void playerNext(void);
// Restarts the track, or goes to the previous one near its start.
void playerPrev(void);
void playerSeek(double delta_seconds);
void playerJumpTo(size_t track);
void playerSetVolume(float volume);
void playerToggleShuffle(void);
void playerCycleRepeat(void);

void playerGetStatus(PlayerStatus *out);
// Copies the full path of a track in the list. False if out of range.
bool playerTrackPath(size_t track, char *out, size_t size);
