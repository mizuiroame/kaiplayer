// Remembers the play queue, the current track and the position, so that
// KaiPlayer reopens where it was left.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "tracklist.h"

#define RESUME_DIR "sdmc:/config/kaiplayer"

void resumeSetDir(const char *dir);  // For tests.

bool resumeSave(const TrackList *queue, size_t track, double position);
// Fills queue, track and position. False if there is nothing to resume.
bool resumeLoad(TrackList *queue, size_t *track, double *position);
void resumeClear(void);
