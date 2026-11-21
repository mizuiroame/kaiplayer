#include "browser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kfile.h"
#include "lang.h"

static void freeEntries(BrowserEntry *entries, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(entries[i].name);
    free(entries);
}

static int compareEntries(const void *pa, const void *pb)
{
    const BrowserEntry *a = pa, *b = pb;
    if (a->is_dir != b->is_dir)
        return a->is_dir ? -1 : 1;
    return utilCompareNames(a->name, b->name);
}

static bool pushEntry(BrowserEntry **entries, size_t *count, size_t *capacity,
                      const char *name, bool is_dir)
{
    if (*count == *capacity) {
        size_t grown_capacity = *capacity ? *capacity * 2 : 64;
        BrowserEntry *grown = realloc(*entries, grown_capacity * sizeof(BrowserEntry));
        if (!grown)
            return false;
        *entries = grown;
        *capacity = grown_capacity;
    }
    char *copy = strdup(name);
    if (!copy)
        return false;
    (*entries)[(*count)++] = (BrowserEntry){copy, is_dir};
    return true;
}

// A network folder's entries, from its listing. False if it can't be listed.
static bool listNetwork(Browser *b, const char *path, BrowserEntry **entries, size_t *count, bool *incomplete)
{
    DavEntry *listed;
    size_t listed_count, capacity = 0;
    if (!kfileList(path, &listed, &listed_count, b->problem, sizeof(b->problem)))
        return false;
    bool ok = true;
    for (size_t i = 0; i < listed_count && ok; i++) {
        const DavEntry *e = &listed[i];
        if (*count >= BROWSER_MAX_ENTRIES) {
            *incomplete = true;
            break;
        }
        if (e->name[0] != '.' && (e->is_dir || utilIsAudioFile(e->name)))
            ok = pushEntry(entries, count, &capacity, e->name, e->is_dir);
    }
    free(listed);
    return ok;
}

// A card folder's entries.
static bool listCard(const char *path, BrowserEntry **entries, size_t *count, bool *incomplete)
{
    DIR *dir = opendir(path);
    if (!dir)
        return false;
    size_t capacity = 0;
    bool ok = true;
    while (ok) {
        if (*count >= BROWSER_MAX_ENTRIES) {
            *incomplete = true;
            break;
        }
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            *incomplete = errno != 0;  // NULL with errno set is a read error, not the end.
            break;
        }
        if (entry->d_name[0] == '.')
            continue;
        bool is_dir = utilEntryIsDir(path, entry);
        if (is_dir || utilIsAudioFile(entry->d_name))
            ok = pushEntry(entries, count, &capacity, entry->d_name, is_dir);
    }
    closedir(dir);
    return ok;
}

bool browserOpen(Browser *b, const char *path)
{
    if (strlen(path) >= sizeof(b->path))
        return false;
    BrowserEntry *entries = NULL;
    size_t count = 0;
    bool incomplete = false;
    b->problem[0] = '\0';
    bool remote = kfileIsRemote(path);
    bool ok = remote ? listNetwork(b, path, &entries, &count, &incomplete) : listCard(path, &entries, &count, &incomplete);
    if (!ok && remote && !b->problem[0])  // No network folder to ask.
        snprintf(b->problem, sizeof(b->problem), "%s", T("Set up the network folder in Settings first."));

    if (!ok) {
        freeEntries(entries, count);
        return false;
    }
    if (count > 1)
        qsort(entries, count, sizeof(BrowserEntry), compareEntries);

    freeEntries(b->entries, b->count);
    strcpy(b->path, path);
    b->entries = entries;
    b->count = count;
    b->cursor = 0;
    b->incomplete = incomplete;
    return true;
}

void browserFree(Browser *b)
{
    freeEntries(b->entries, b->count);
    b->entries = NULL;
    b->count = 0;
    b->cursor = 0;
}

bool browserGoUp(Browser *b)
{
    char parent[UTIL_PATH_MAX];
    strcpy(parent, b->path);
    if (!utilParentPath(parent))
        return false;

    char left[UTIL_PATH_MAX];
    strcpy(left, utilFileName(b->path));
    if (!browserOpen(b, parent))
        return false;

    for (size_t i = 0; i < b->count; i++) {
        if (b->entries[i].is_dir && strcmp(b->entries[i].name, left) == 0) {
            b->cursor = i;
            break;
        }
    }
    return true;
}

void browserMove(Browser *b, int delta)
{
    if (b->count == 0)
        return;
    long next = (long)b->cursor + delta;
    if (next < 0)
        next = 0;
    if (next >= (long)b->count)
        next = (long)b->count - 1;
    b->cursor = (size_t)next;
}

const BrowserEntry *browserSelected(const Browser *b)
{
    return b->count ? &b->entries[b->cursor] : NULL;
}
