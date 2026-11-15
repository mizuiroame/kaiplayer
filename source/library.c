#include "library.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

// Root folders of the console, kaiHEN and other homebrew. Their sound effects
// should not end up in a play list.
static const char *const SYSTEM_FOLDERS[] = {
    "atmosphere", "bootloader", "config", "emuMMC", "game", "hen", "licenses",
    "Nintendo", "programs", "ro", "switch", "themes",
};

bool libraryIsSystemFolder(const char *parent, const char *name)
{
    size_t len = strlen(parent);
    bool parent_is_root = len >= 2 && parent[len - 1] == '/' && parent[len - 2] == ':';
    if (!parent_is_root)
        return false;
    for (size_t i = 0; i < sizeof(SYSTEM_FOLDERS) / sizeof(SYSTEM_FOLDERS[0]); i++) {
        if (utilCompareNames(name, SYSTEM_FOLDERS[i]) == 0)
            return true;
    }
    return false;
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
            if (recursive && depth < LIBRARY_MAX_DEPTH &&
                !libraryIsSystemFolder(folder, entry->d_name))
                ok = scanDir(list, path, recursive, depth + 1, budget);
        } else if (utilIsAudioFile(entry->d_name)) {
            ok = trackListAdd(list, path);
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
