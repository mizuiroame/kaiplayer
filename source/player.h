// Playback engine: a thread decodes the current track, resamples it to the
// output rate and feeds audout. The UI calls these functions from the main
// thread; they only set state, so they return at once.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "decoder.h"
#include "tracklist.h"
#include "eq.h"
#include "queue.h"
#include "replaygain.h"

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
    bool fading;          // A fade-out to pause is running.
    size_t track_count;
    double position;      // Seconds.
    double duration;      // Seconds, 0 if unknown.
    bool shuffle;
    RepeatMode repeat;
    AudioFormat format;   // The playing file's format; an empty codec when none.
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
// Seeks to an absolute position in seconds.
void playerSeekTo(double seconds);
// Lowers the volume to silence over `seconds`, then pauses. Any other
// playback command (pause, next, jump...) cancels the fade.
void playerFadeOutAndPause(double seconds);
void playerJumpTo(size_t track);
void playerToggleShuffle(void);
void playerSetShuffle(bool shuffle);
void playerCycleRepeat(void);

void playerGetStatus(PlayerStatus *out);
// The track at a position of the play order (shuffled or not).
// False if out of range.
bool playerQueueTrack(size_t pos, size_t *track);

// The equalizer: off, or on with these gains (dB per band).
void playerSetEqualizer(bool enabled, const int gains[EQ_BANDS]);
// ReplayGain: applies from the next track that opens.
void playerSetReplayGain(ReplayGainMode mode);
// Tells Auto ReplayGain whether the queue mixes albums (a playlist).
void playerSetMixedAlbums(bool mixed);

// Copies the full path of a track in the list. False if out of range.
bool playerTrackPath(size_t track, char *out, size_t size);
