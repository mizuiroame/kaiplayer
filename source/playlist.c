#include "playlist.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "util.h"

#define HEADER "#EXTM3U\n"
#define UTF8_BOM "\xEF\xBB\xBF"
#define NAME_PUNCTUATION " -_.()'&!+,"

static char g_root[UTIL_PATH_MAX] = PLAYLIST_ROOT;

void playlistSetRoot(const char *root)
{
    snprintf(g_root, sizeof(g_root), "%s", root);
}

const char *playlistRoot(void)
{
    return g_root;
}

bool playlistParse(const char *text, size_t length, const char *base_dir, TrackList *out)
{
    *out = (TrackList){0};
    const char *p = text, *end = text + length;
    if (length >= 3 && memcmp(p, UTF8_BOM, 3) == 0)
        p += 3;
    char line[UTIL_PATH_MAX], path[UTIL_PATH_MAX];
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t n = nl ? (size_t)(nl - p) : (size_t)(end - p);
        const char *next = nl ? nl + 1 : end;
        while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t'))
            n--;
        while (n > 0 && (*p == ' ' || *p == '\t'))
            p++, n--;
        bool usable = n > 0 && n < sizeof(line) && p[0] != '#';
        if (usable) {
            memcpy(line, p, n);
            line[n] = '\0';
            if (utilResolvePath(base_dir, line, path, sizeof(path)) && !trackListAdd(out, path)) {
                trackListFree(out);
                return false;
            }
        }
        p = next;
    }
    return true;
}

char *playlistFormat(const TrackList *list, const char *base_dir)
{
    size_t capacity = sizeof(HEADER) + list->count * (UTIL_PATH_MAX + 1);
    char *text = malloc(capacity);
    if (!text)
        return NULL;
    size_t pos = (size_t)snprintf(text, capacity, "%s", HEADER);
    char rel[UTIL_PATH_MAX];
    for (size_t i = 0; i < list->count; i++) {
        const char *entry = utilRelativePath(base_dir, list->paths[i], rel, sizeof(rel)) ? rel : list->paths[i];
        pos += (size_t)snprintf(text + pos, capacity - pos, "%s\n", entry);
    }
    return text;
}

// The folder that holds a file path.
static void folderOf(const char *path, char *out, size_t size)
{
    snprintf(out, size, "%s", path);
    if (!utilParentPath(out))
        out[0] = '\0';
}

bool playlistLoad(const char *path, TrackList *out)
{
    *out = (TrackList){0};
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char *text = NULL;
    long len = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        len = ftell(f);
    bool ok = len >= 0 && (unsigned long)len <= PLAYLIST_FILE_MAX && fseek(f, 0, SEEK_SET) == 0 &&
              (text = malloc((size_t)len + 1)) != NULL && fread(text, 1, (size_t)len, f) == (size_t)len;
    fclose(f);
    if (ok) {
        char base[UTIL_PATH_MAX];
        folderOf(path, base, sizeof(base));
        ok = playlistParse(text, (size_t)len, base, out);
    }
    free(text);
    return ok;
}

bool playlistSave(const char *path, const TrackList *list)
{
    char base[UTIL_PATH_MAX], tmp[UTIL_PATH_MAX];
    folderOf(path, base, sizeof(base));
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
        return false;
    char *text = playlistFormat(list, base);
    if (!text)
        return false;
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(text, 1, strlen(text), f) == strlen(text);
    if (f)
        ok = fclose(f) == 0 && ok;
    free(text);
    if (ok) {
        remove(path);  // rename() doesn't replace on every filesystem.
        ok = rename(tmp, path) == 0;
    }
    if (!ok)
        remove(tmp);
    return ok;
}

bool playlistNameValid(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > PLAYLIST_NAME_MAX || name[0] == '.' || name[0] == ' ' || name[n - 1] == ' ')
        return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  (c && strchr(NAME_PUNCTUATION, c));
        if (!ok)
            return false;
    }
    return true;
}

bool playlistPathFor(const char *name, char *out, size_t size)
{
    int n = snprintf(out, size, "%s/%s%s", g_root, name, PLAYLIST_EXT);
    return playlistNameValid(name) && n > 0 && (size_t)n < size;
}

bool playlistExists(const char *name)
{
    char path[UTIL_PATH_MAX];
    struct stat st;
    return playlistPathFor(name, path, sizeof(path)) && stat(path, &st) == 0;
}

static int compareNames(const void *a, const void *b)
{
    return utilCompareNames(*(char *const *)a, *(char *const *)b);
}

bool playlistIndexLoad(PlaylistIndex *index)
{
    *index = (PlaylistIndex){0};
    DIR *dir = opendir(g_root);
    if (!dir)
        return true;  // No folder yet: no playlists.
    char **names = calloc(PLAYLIST_MAX_COUNT, sizeof(char *));
    bool ok = names != NULL;
    struct dirent *entry;
    while (ok && index->count < PLAYLIST_MAX_COUNT && (entry = readdir(dir)) != NULL) {
        const char *dot = strrchr(entry->d_name, '.');
        if (entry->d_name[0] == '.' || !dot || (utilCompareNames(dot, ".m3u8") != 0 && utilCompareNames(dot, ".m3u") != 0))
            continue;
        size_t n = (size_t)(dot - entry->d_name);
        char *name = malloc(n + 1);
        ok = name != NULL;
        if (ok) {
            memcpy(name, entry->d_name, n);
            name[n] = '\0';
            names[index->count++] = name;
        }
    }
    closedir(dir);
    index->names = names;
    if (!ok) {
        playlistIndexFree(index);
        return false;
    }
    if (index->count > 1)
        qsort(index->names, index->count, sizeof(char *), compareNames);
    return true;
}

void playlistIndexFree(PlaylistIndex *index)
{
    for (size_t i = 0; i < index->count; i++)
        free(index->names[i]);
    free(index->names);
    *index = (PlaylistIndex){0};
}

bool playlistCreate(const char *name)
{
    char path[UTIL_PATH_MAX];
    if (!playlistPathFor(name, path, sizeof(path)) || playlistExists(name))
        return false;
    mkdir(g_root, 0777);
    TrackList empty = {0};
    return playlistSave(path, &empty);
}

bool playlistRename(const char *name, const char *new_name)
{
    char from[UTIL_PATH_MAX], to[UTIL_PATH_MAX];
    if (!playlistPathFor(name, from, sizeof(from)) || !playlistPathFor(new_name, to, sizeof(to)))
        return false;
    if (utilCompareNames(name, new_name) != 0 && playlistExists(new_name))
        return false;
    return rename(from, to) == 0;
}

bool playlistDelete(const char *name)
{
    char path[UTIL_PATH_MAX];
    return playlistPathFor(name, path, sizeof(path)) && remove(path) == 0;
}

int playlistAddTracks(const char *name, const TrackList *tracks)
{
    char path[UTIL_PATH_MAX];
    TrackList list;
    if (!playlistPathFor(name, path, sizeof(path)) || !playlistLoad(path, &list))
        return -1;
    int added = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < tracks->count; i++) {
        if (trackListFind(&list, tracks->paths[i]) < list.count || list.count >= TRACKLIST_MAX)
            continue;
        ok = trackListAdd(&list, tracks->paths[i]);
        added += ok;
    }
    ok = ok && playlistSave(path, &list);
    trackListFree(&list);
    return ok ? added : -1;
}
