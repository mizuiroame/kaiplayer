#include "resume.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "playlist.h"
#include "util.h"

#define QUEUE_FILE "resume.m3u8"
#define STATE_FILE "resume.txt"

static char g_dir[UTIL_PATH_MAX] = RESUME_DIR;

void resumeSetDir(const char *dir)
{
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
}

static bool filePath(const char *name, char *out, size_t size)
{
    return utilJoinPath(out, size, g_dir, name);
}

// Creates g_dir and its parent ("sdmc:/config/kaiplayer").
static void makeDirs(void)
{
    char parent[UTIL_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", g_dir);
    if (utilParentPath(parent))
        mkdir(parent, 0777);
    mkdir(g_dir, 0777);
}

bool resumeSave(const TrackList *queue, size_t track, double position)
{
    char queue_path[UTIL_PATH_MAX], state_path[UTIL_PATH_MAX];
    if (!filePath(QUEUE_FILE, queue_path, sizeof(queue_path)) ||
        !filePath(STATE_FILE, state_path, sizeof(state_path)) || queue->count == 0)
        return false;
    makeDirs();
    if (!playlistSave(queue_path, queue))
        return false;
    FILE *f = fopen(state_path, "w");
    if (!f)
        return false;
    fprintf(f, "track=%zu\nposition=%.1f\n", track, position < 0 ? 0.0 : position);
    return fclose(f) == 0;
}

static void readState(size_t *track, double *position)
{
    char path[UTIL_PATH_MAX], line[64];
    *track = 0;
    *position = 0.0;
    if (!filePath(STATE_FILE, path, sizeof(path)))
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "track=", 6) == 0)
            *track = (size_t)strtoul(line + 6, NULL, 10);
        else if (strncmp(line, "position=", 9) == 0)
            *position = strtod(line + 9, NULL);
    }
    fclose(f);
}

bool resumeLoad(TrackList *queue, size_t *track, double *position)
{
    char path[UTIL_PATH_MAX];
    *queue = (TrackList){0};
    if (!filePath(QUEUE_FILE, path, sizeof(path)) || !playlistLoad(path, queue))
        return false;
    if (queue->count == 0) {
        trackListFree(queue);
        return false;
    }
    readState(track, position);
    if (*track >= queue->count)
        *track = 0;
    if (!(*position >= 0.0) || *position > 24 * 3600.0)
        *position = 0.0;
    return true;
}

void resumeClear(void)
{
    char path[UTIL_PATH_MAX];
    if (filePath(QUEUE_FILE, path, sizeof(path)))
        remove(path);
    if (filePath(STATE_FILE, path, sizeof(path)))
        remove(path);
}
