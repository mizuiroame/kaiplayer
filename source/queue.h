// Play order over a track list: sequential or shuffled, with repeat modes.
// Pure logic, with no I/O, so it can be tested on the host.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    REPEAT_OFF,
    REPEAT_ALL,
    REPEAT_ONE,
    REPEAT_MODE_COUNT,
} RepeatMode;

typedef struct {
    size_t count;
    size_t *order;  // order[pos] is a track index.
    size_t pos;
    bool shuffle;
    RepeatMode repeat;
    uint64_t rng;
} Queue;

// Starts at track start_track. Returns false if allocation fails or count is 0.
bool queueInit(Queue *q, size_t count, size_t start_track, bool shuffle, uint64_t seed);
void queueFree(Queue *q);

size_t queueCurrent(const Queue *q);

// Moves to the next track. A user skip always moves and wraps; an automatic
// advance honors the repeat mode and returns false at the end of the list.
bool queueNext(Queue *q, bool by_user);
void queuePrev(Queue *q);

// Turns shuffle on or off. The current track stays current.
void queueSetShuffle(Queue *q, bool shuffle);

// Makes track the current one.
void queueJumpTo(Queue *q, size_t track);
