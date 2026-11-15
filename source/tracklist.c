#include "tracklist.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 64

void trackListFree(TrackList *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->paths[i]);
    free(list->paths);
    *list = (TrackList){0};
}

bool trackListAdd(TrackList *list, const char *path)
{
    if (list->count >= TRACKLIST_MAX)
        return true;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : INITIAL_CAPACITY;
        char **grown = realloc(list->paths, capacity * sizeof(char *));
        if (!grown)
            return false;
        list->paths = grown;
        list->capacity = capacity;
    }
    char *copy = strdup(path);
    if (!copy)
        return false;
    list->paths[list->count++] = copy;
    return true;
}

bool trackListAddAll(TrackList *list, const TrackList *src)
{
    for (size_t i = 0; i < src->count; i++) {
        if (!trackListAdd(list, src->paths[i]))
            return false;
    }
    return true;
}

bool trackListCopy(TrackList *out, const TrackList *src)
{
    *out = (TrackList){0};
    if (!trackListAddAll(out, src)) {
        trackListFree(out);
        return false;
    }
    return true;
}

void trackListRemove(TrackList *list, size_t index)
{
    if (index >= list->count)
        return;
    free(list->paths[index]);
    memmove(list->paths + index, list->paths + index + 1, (list->count - index - 1) * sizeof(char *));
    list->count--;
}

void trackListMove(TrackList *list, size_t from, size_t to)
{
    if (from >= list->count || to >= list->count || from == to)
        return;
    char *moved = list->paths[from];
    if (from < to)
        memmove(list->paths + from, list->paths + from + 1, (to - from) * sizeof(char *));
    else
        memmove(list->paths + to + 1, list->paths + to, (from - to) * sizeof(char *));
    list->paths[to] = moved;
}

size_t trackListFind(const TrackList *list, const char *path)
{
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->paths[i], path) == 0)
            return i;
    }
    return list->count;
}
