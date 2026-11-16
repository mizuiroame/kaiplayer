#include "playlist.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "plainname.h"
#include "util.h"

#define HEADER "#EXTM3U\n"
#define UTF8_BOM "\xEF\xBB\xBF"
#define TITLE_TAG "#PLAYLIST:"  // The name, when the file name can't carry it.

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

char *playlistFormat(const TrackList *list, const char *base_dir, const char *title)
{
    size_t capacity = sizeof(HEADER) + sizeof(TITLE_TAG) + PLAYLIST_NAME_MAX + 1 + list->count * (UTIL_PATH_MAX + 1);
    char *text = malloc(capacity);
    if (!text)
        return NULL;
    size_t pos = (size_t)snprintf(text, capacity, "%s", HEADER);
    if (title)
        pos += (size_t)snprintf(text + pos, capacity - pos, "%s%s\n", TITLE_TAG, title);
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

// Writes a playlist atomically, with its title when it has one.
static bool saveAs(const char *path, const TrackList *list, const char *title)
{
    char base[UTIL_PATH_MAX], tmp[UTIL_PATH_MAX];
    folderOf(path, base, sizeof(base));
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
        return false;
    char *text = playlistFormat(list, base, title);
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

// The title in a "#PLAYLIST:" line among the first lines of a playlist.
static bool readTitle(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char line[PLAYLIST_NAME_MAX + 32];
    bool found = false;
    for (int i = 0; i < 4 && fgets(line, sizeof(line), f); i++) {
        char *p = line;
        if (i == 0 && memcmp(p, UTF8_BOM, 3) == 0)
            p += 3;
        p[strcspn(p, "\r\n")] = '\0';
        if (strncmp(p, TITLE_TAG, strlen(TITLE_TAG)) == 0) {
            p += strlen(TITLE_TAG);
            found = playlistNameValid(p) && (size_t)snprintf(out, size, "%s", p) < size;
            break;
        }
        if (p[0] != '#')
            break;  // The songs have started.
    }
    fclose(f);
    return found;
}

bool playlistSave(const char *path, const TrackList *list)
{
    char title[PLAYLIST_NAME_MAX + 1];
    return saveAs(path, list, readTitle(path, title, sizeof(title)) ? title : NULL);
}

bool playlistNameValid(const char *name)
{
    size_t n = strlen(name), chars = 0;
    if (n == 0 || n > PLAYLIST_NAME_MAX || name[0] == '.' || name[0] == ' ' || name[n - 1] == ' ')
        return false;
    for (const char *p = name; *p;) {
        const char *at = p;
        unsigned cp = utilNextCodePoint(&p);
        bool bad_utf8 = cp == '?' && *at != '?';
        if (bad_utf8 || cp < 0x20 || cp == 0x7F || cp == '/' || cp == '\\' || ++chars > PLAYLIST_NAME_CHARS)
            return false;
    }
    return true;
}

// Calls visit for each playlist file in the root folder, with its file
// name, that name without the extension and the playlist's title, until
// visit returns true. True if one did.
typedef bool (*Visit)(const char *file, const char *stem, const char *title, void *ctx);

static bool isPlaylistFile(const char *dot)
{
    return utilCompareNames(dot, ".m3u8") == 0 || utilCompareNames(dot, ".m3u") == 0;
}

static bool eachPlaylist(Visit visit, void *ctx)
{
    DIR *dir = opendir(g_root);
    if (!dir)
        return false;
    bool stopped = false;
    struct dirent *entry;
    while (!stopped && (entry = readdir(dir)) != NULL) {
        const char *dot = strrchr(entry->d_name, '.');
        size_t n = dot ? (size_t)(dot - entry->d_name) : 0;
        if (entry->d_name[0] == '.' || !dot || !isPlaylistFile(dot) || n > PLAYLIST_NAME_MAX)
            continue;
        char stem[PLAYLIST_NAME_MAX + 1], path[UTIL_PATH_MAX], title[PLAYLIST_NAME_MAX + 1];
        memcpy(stem, entry->d_name, n);
        stem[n] = '\0';
        if (snprintf(path, sizeof(path), "%s/%s", g_root, entry->d_name) >= (int)sizeof(path))
            continue;
        if (!readTitle(path, title, sizeof(title)))
            snprintf(title, sizeof(title), "%s", stem);
        stopped = visit(entry->d_name, stem, title, ctx);
    }
    closedir(dir);
    return stopped;
}

typedef struct {
    const char *want;  // A title, or a file name without the extension.
    char *file;
    size_t size;
} Search;

static bool titleMatches(const char *file, const char *stem, const char *title, void *ctx)
{
    (void)stem;
    Search *s = ctx;
    return utilCompareNames(title, s->want) == 0 && (size_t)snprintf(s->file, s->size, "%s", file) < s->size;
}

static bool stemMatches(const char *file, const char *stem, const char *title, void *ctx)
{
    (void)file, (void)title;
    return utilCompareNames(stem, ((Search *)ctx)->want) == 0;
}

// The file of the playlist with this title.
static bool fileOf(const char *title, char *file, size_t size)
{
    Search s = {title, file, size};
    return eachPlaylist(titleMatches, &s);
}

static bool stemTaken(const char *stem)
{
    Search s = {stem, NULL, 0};
    return eachPlaylist(stemMatches, &s);
}

// A free plain file name for a new playlist with this title:
// "Español" -> "Espanol.m3u8", or "Espanol (2).m3u8" if that one is taken.
static void newFileFor(const char *title, char *file, size_t size)
{
    char base[PLAYLIST_FILE_NAME_MAX + 1], stem[PLAYLIST_FILE_NAME_MAX + 16];
    plainName(title, "Playlist", base, sizeof(base));
    snprintf(stem, sizeof(stem), "%s", base);
    for (int n = 2; stemTaken(stem) && n < 10000; n++)
        snprintf(stem, sizeof(stem), "%s (%d)", base, n);
    snprintf(file, size, "%s%s", stem, PLAYLIST_EXT);
}

static bool inRoot(const char *file, char *out, size_t size)
{
    int n = snprintf(out, size, "%s/%s", g_root, file);
    return n > 0 && (size_t)n < size;
}

// The title to write in a file: none when the file name already says it.
static const char *titleFor(const char *title, const char *file)
{
    size_t n = strlen(file) - strlen(PLAYLIST_EXT);
    return strncmp(title, file, n) == 0 && title[n] == '\0' ? NULL : title;
}

bool playlistPathFor(const char *name, char *out, size_t size)
{
    char file[PLAYLIST_NAME_MAX + 32];
    if (!playlistNameValid(name))
        return false;
    if (!fileOf(name, file, sizeof(file)))
        newFileFor(name, file, sizeof(file));
    return inRoot(file, out, size);
}

bool playlistExists(const char *name)
{
    char file[PLAYLIST_NAME_MAX + 32];
    return playlistNameValid(name) && fileOf(name, file, sizeof(file));
}

static int compareNames(const void *a, const void *b)
{
    return utilCompareNames(*(char *const *)a, *(char *const *)b);
}

static bool collectTitle(const char *file, const char *stem, const char *title, void *ctx)
{
    (void)file, (void)stem;
    PlaylistIndex *index = ctx;
    char *copy = index->count < PLAYLIST_MAX_COUNT ? strdup(title) : NULL;
    if (!copy)
        return true;  // Full, or out of memory: keep what's listed.
    index->names[index->count++] = copy;
    return false;
}

bool playlistIndexLoad(PlaylistIndex *index)
{
    *index = (PlaylistIndex){0};
    index->names = calloc(PLAYLIST_MAX_COUNT, sizeof(char *));
    if (!index->names)
        return false;
    eachPlaylist(collectTitle, index);  // No folder yet: no playlists.
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
    char file[PLAYLIST_NAME_MAX + 32], path[UTIL_PATH_MAX];
    if (!playlistNameValid(name) || playlistExists(name))
        return false;
    mkdir(g_root, 0777);
    newFileFor(name, file, sizeof(file));
    TrackList empty = {0};
    return inRoot(file, path, sizeof(path)) && saveAs(path, &empty, titleFor(name, file));
}

bool playlistRename(const char *name, const char *new_name)
{
    char file[PLAYLIST_NAME_MAX + 32], to_file[PLAYLIST_NAME_MAX + 32];
    char from[UTIL_PATH_MAX], to[UTIL_PATH_MAX], plain[PLAYLIST_FILE_NAME_MAX + 1];
    if (!playlistNameValid(new_name) || !fileOf(name, file, sizeof(file)) || !inRoot(file, from, sizeof(from)))
        return false;
    if (utilCompareNames(name, new_name) != 0 && playlistExists(new_name))
        return false;
    // The same file when the plain name stays ("Espanol" -> "Español").
    plainName(new_name, "Playlist", plain, sizeof(plain));
    size_t stem = (size_t)(strrchr(file, '.') - file);
    if (strlen(plain) == stem && strncasecmp(plain, file, stem) == 0)
        snprintf(to_file, sizeof(to_file), "%s", file);
    else
        newFileFor(new_name, to_file, sizeof(to_file));
    TrackList list;
    if (!inRoot(to_file, to, sizeof(to)) || !playlistLoad(from, &list))
        return false;
    bool ok = saveAs(to, &list, titleFor(new_name, to_file));
    trackListFree(&list);
    if (ok && strcmp(from, to) != 0)
        remove(from);
    return ok;
}

bool playlistDelete(const char *name)
{
    char file[PLAYLIST_NAME_MAX + 32], path[UTIL_PATH_MAX];
    return fileOf(name, file, sizeof(file)) && inRoot(file, path, sizeof(path)) && remove(path) == 0;
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
