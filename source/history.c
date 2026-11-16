#include "history.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

void historyInit(History *h)
{
    memset(h, 0, sizeof(*h));
}

void historyFree(History *h)
{
    for (size_t i = 0; i < h->count; i++)
        free(h->items[i].key);
    free(h->items);
    historyInit(h);
}

bool historyAdd(History *h, HistoryKind kind, const char *key)
{
    if (!key[0] || strpbrk(key, "\t\r\n"))
        return false;  // Wouldn't survive the file.
    if (h->count == HISTORY_MAX) {  // Drop the oldest.
        free(h->items[0].key);
        memmove(h->items, h->items + 1, (h->count - 1) * sizeof(HistoryEntry));
        h->count--;
    }
    if (h->count == h->capacity) {
        size_t grown = h->capacity ? h->capacity * 2 : 64;
        grown = grown > HISTORY_MAX ? HISTORY_MAX : grown;
        HistoryEntry *items = realloc(h->items, grown * sizeof(HistoryEntry));
        if (!items)
            return false;
        h->items = items;
        h->capacity = grown;
    }
    char *copy = strdup(key);
    if (!copy)
        return false;
    h->items[h->count++] = (HistoryEntry){kind, copy};
    return true;
}

void historyWriteEntry(FILE *f, HistoryKind kind, const char *key)
{
    fprintf(f, "%c\t%s\n", kind == HISTORY_PLAYLIST ? 'P' : 'S', key);
}

bool historySave(const History *h, FILE *f)
{
    for (size_t i = 0; i < h->count; i++)
        historyWriteEntry(f, h->items[i].kind, h->items[i].key);
    return !ferror(f);
}

bool historyLoad(History *h, FILE *f)
{
    char line[UTIL_PATH_MAX + 8];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        const char *key = line + 2;
        if ((line[0] != 'S' && line[0] != 'P') || line[1] != '\t' || !key[0] || strchr(key, '\t'))
            continue;  // A damaged line.
        if (!historyAdd(h, line[0] == 'P' ? HISTORY_PLAYLIST : HISTORY_SONG, key))
            return false;  // Out of memory.
    }
    return true;
}
