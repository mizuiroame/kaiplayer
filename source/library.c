#include "library.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

static bool appendPath(TrackList *list, const char *path)
{
    if (list->count >= LIBRARY_MAX_TRACKS)
        return true;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 64;
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

static bool scanDir(TrackList *list, const char *folder, bool recursive, int depth, size_t *budget)
{
    DIR *dir = opendir(folder);
    if (!dir)
        return true;  // Unreadable folders are skipped, not fatal.

    bool ok = true;
    char path[UTIL_PATH_MAX];
    struct dirent *entry;
    while (ok && *budget > 0 && (entry = readdir(dir)) != NULL) {
        (*budget)--;
        if (entry->d_name[0] == '.')
            continue;  // ".", ".." and hidden files such as macOS "._x.mp3".
        if (!utilJoinPath(path, sizeof(path), folder, entry->d_name))
            continue;
        if (utilEntryIsDir(folder, entry)) {
            if (recursive && depth < LIBRARY_MAX_DEPTH)
                ok = scanDir(list, path, recursive, depth + 1, budget);
        } else if (utilIsAudioFile(entry->d_name)) {
            ok = appendPath(list, path);
        }
    }
    closedir(dir);
    return ok;
}

static int comparePaths(const void *a, const void *b)
{
    return utilCompareNames(*(char *const *)a, *(char *const *)b);
}

bool libraryScan(TrackList *out, const char *folder, bool recursive)
{
    *out = (TrackList){0};
    size_t budget = LIBRARY_MAX_ENTRIES;
    if (!scanDir(out, folder, recursive, 0, &budget)) {
        trackListFree(out);
        return false;
    }
    if (out->count > 1)
        qsort(out->paths, out->count, sizeof(char *), comparePaths);
    return true;
}

void trackListFree(TrackList *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->paths[i]);
    free(list->paths);
    *list = (TrackList){0};
}

size_t trackListFind(const TrackList *list, const char *path)
{
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->paths[i], path) == 0)
            return i;
    }
    return list->count;
}
