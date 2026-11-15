#include "queue.h"

#include <stdlib.h>

static uint64_t nextRandom(Queue *q)
{
    // xorshift64*: good enough for shuffling and deterministic in tests.
    uint64_t x = q->rng ? q->rng : 0x9E3779B97F4A7C15ull;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    q->rng = x;
    return x * 0x2545F4914F6CDD1Dull;
}

// Fills order with 0..count-1, shuffled if asked, with first at position 0.
static void buildOrder(Queue *q, size_t first)
{
    for (size_t i = 0; i < q->count; i++)
        q->order[i] = i;

    if (q->shuffle) {
        for (size_t i = q->count - 1; i > 0; i--) {
            size_t j = (size_t)(nextRandom(q) % (i + 1));
            size_t t = q->order[i];
            q->order[i] = q->order[j];
            q->order[j] = t;
        }
        for (size_t i = 0; i < q->count; i++) {
            if (q->order[i] == first) {
                q->order[i] = q->order[0];
                q->order[0] = first;
                break;
            }
        }
        q->pos = 0;
    } else {
        q->pos = first;
    }
}

bool queueInit(Queue *q, size_t count, size_t start_track, bool shuffle, uint64_t seed)
{
    q->order = NULL;
    q->count = 0;
    q->pos = 0;
    q->repeat = REPEAT_OFF;
    if (count == 0)
        return false;
    q->order = malloc(count * sizeof(size_t));
    if (!q->order)
        return false;
    q->count = count;
    q->shuffle = shuffle;
    q->rng = seed;
    buildOrder(q, start_track < count ? start_track : 0);
    return true;
}

void queueFree(Queue *q)
{
    free(q->order);
    q->order = NULL;
    q->count = 0;
    q->pos = 0;
}

size_t queueCurrent(const Queue *q)
{
    return q->count ? q->order[q->pos] : 0;
}

bool queueNext(Queue *q, bool by_user)
{
    if (q->count == 0)
        return false;
    if (!by_user && q->repeat == REPEAT_ONE)
        return true;
    if (q->pos + 1 < q->count) {
        q->pos++;
        return true;
    }
    if (!by_user && q->repeat == REPEAT_OFF)
        return false;
    q->pos = 0;
    return true;
}

void queuePrev(Queue *q)
{
    if (q->count == 0)
        return;
    q->pos = q->pos > 0 ? q->pos - 1 : q->count - 1;
}

void queueSetShuffle(Queue *q, bool shuffle)
{
    if (q->count == 0) {
        q->shuffle = shuffle;
        return;
    }
    size_t current = queueCurrent(q);
    q->shuffle = shuffle;
    buildOrder(q, current);
}

void queueJumpTo(Queue *q, size_t track)
{
    for (size_t i = 0; i < q->count; i++) {
        if (q->order[i] == track) {
            q->pos = i;
            return;
        }
    }
}
