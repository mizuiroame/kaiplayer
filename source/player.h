// Playback engine: a thread decodes the current track, resamples it to the
// output rate and feeds audout. The UI calls these functions from the main
// thread; they only set state, so they return at once.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "tracklist.h"
#include "queue.h"

typedef enum {
    PLAYER_STOPPED,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} PlayerState;

typedef struct {
    PlayerState state;
    size_t track;         // Index in the track list.
    size_t queue_pos;     // Position of that track in the play order.
    unsigned generation;  // Changes whenever a new list starts playing.
    size_t track_count;
    double position;      // Seconds.
    double duration;      // Seconds, 0 if unknown.
    bool shuffle;
    RepeatMode repeat;
    char error[160];
} PlayerStatus;

bool playerInit(bool shuffle, RepeatMode repeat);
void playerExit(void);

// Plays list from track start. The player takes the list's contents.
void playerPlay(TrackList *list, size_t start);

// Loads list paused at track and position (seconds), as it was left.
// The player takes the list's contents.
void playerRestore(TrackList *list, size_t track, double position);

// Copies the play list, the current track and the position. False if
// nothing is loaded or memory runs out.
bool playerSnapshot(TrackList *out, size_t *track, double *position);

void playerTogglePause(void);
void playerNext(void);
// Restarts the track, or goes to the previous one near its start.
void playerPrev(void);
void playerSeek(double delta_seconds);
void playerJumpTo(size_t track);
void playerToggleShuffle(void);
void playerSetShuffle(bool shuffle);
void playerCycleRepeat(void);

void playerGetStatus(PlayerStatus *out);
// The track at a position of the play order (shuffled or not).
// False if out of range.
bool playerQueueTrack(size_t pos, size_t *track);

// Copies the full path of a track in the list. False if out of range.
bool playerTrackPath(size_t track, char *out, size_t size);
