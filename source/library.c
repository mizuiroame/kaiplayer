#include "library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kfile.h"
#include "util.h"

#define WHY_MAX 160

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

// --- A network folder ---

typedef struct {
    LibraryFiles *out;
    bool (*stop)(void);
    int folders;   // Asked for so far.
    size_t budget; // Entries left to look at.
    bool recursive, ok;
} NetworkScan;

static bool addFile(LibraryFiles *out, const char *path, const DavEntry *e)
{
    if (out->count >= LIBRARY_MAX_TRACKS)
        return true;  // As a full track list: the rest is left out.
    if (out->count == out->capacity) {
        size_t grown = out->capacity ? out->capacity * 2 : 256;
        LibraryFile *files = realloc(out->files, grown * sizeof(LibraryFile));
        if (!files)
            return false;
        out->files = files;
        out->capacity = grown;
    }
    char *copy = strdup(path);
    if (!copy)
        return false;
    out->files[out->count++] = (LibraryFile){copy, e->size, e->modified};
    return true;
}

// False if this folder couldn't be listed (a folder under it that can't be is skipped).
static bool scanNetworkFolder(NetworkScan *s, const char *folder, int depth, char *why, size_t why_size)
{
    DavEntry *entries = NULL;
    size_t count = 0;
    s->folders++;
    if (!kfileList(folder, &entries, &count, why, why_size))
        return false;
    char path[UTIL_PATH_MAX], skipped[WHY_MAX];
    for (size_t i = 0; i < count && s->ok && s->budget > 0; i++) {
        const DavEntry *e = &entries[i];
        s->budget--;
        if (e->name[0] == '.' || !utilJoinPath(path, sizeof(path), folder, e->name))
            continue;  // Hidden files such as macOS "._x.mp3", and paths too long to keep.
        if (e->is_dir && s->recursive && depth < LIBRARY_MAX_DEPTH && s->folders < LIBRARY_NETWORK_FOLDERS &&
            !(s->stop && s->stop()))
            scanNetworkFolder(s, path, depth + 1, skipped, sizeof(skipped));
        else if (!e->is_dir && utilIsAudioFile(e->name))
            s->ok = addFile(s->out, path, e);
    }
    free(entries);
    return true;
}

static int compareFiles(const void *a, const void *b)
{
    return utilCompareNames(((const LibraryFile *)a)->path, ((const LibraryFile *)b)->path);
}

void libraryFilesFree(LibraryFiles *files)
{
    for (size_t i = 0; i < files->count; i++)
        free(files->files[i].path);
    free(files->files);
    *files = (LibraryFiles){0};
}

static bool scanNetwork(LibraryFiles *out, const char *folder, bool recursive, bool (*stop)(void), char *why,
                        size_t why_size)
{
    *out = (LibraryFiles){0};
    char ignored[WHY_MAX];
    NetworkScan s = {out, stop, 0, LIBRARY_MAX_ENTRIES, recursive, true};
    bool ok = scanNetworkFolder(&s, folder, 0, why ? why : ignored, why ? why_size : sizeof(ignored)) && s.ok &&
              !(stop && stop());
    if (!ok) {
        libraryFilesFree(out);
        return false;
    }
    if (out->count > 1)
        qsort(out->files, out->count, sizeof(LibraryFile), compareFiles);
    return true;
}

bool libraryScanNetwork(LibraryFiles *out, const char *folder, bool (*stop)(void), char *why, size_t why_size)
{
    return scanNetwork(out, folder, true, stop, why, why_size);
}

// The same as a track list: its paths, sorted.
static bool scanNetworkTracks(TrackList *out, const char *folder, bool recursive)
{
    LibraryFiles files;
    if (!scanNetwork(&files, folder, recursive, NULL, NULL, 0))
        return false;
    bool ok = true;
    for (size_t i = 0; i < files.count && ok; i++)
        ok = trackListAdd(out, files.files[i].path);
    libraryFilesFree(&files);
    if (!ok)
        trackListFree(out);
    return ok;
}

bool libraryScan(TrackList *out, const char *folder, bool recursive)
{
    *out = (TrackList){0};
    if (kfileIsRemote(folder))
        return scanNetworkTracks(out, folder, recursive);
    size_t budget = LIBRARY_MAX_ENTRIES;
    if (!scanDir(out, folder, recursive, 0, &budget)) {
        trackListFree(out);
        return false;
    }
    if (out->count > 1)
        qsort(out->paths, out->count, sizeof(char *), comparePaths);
    return true;
}
