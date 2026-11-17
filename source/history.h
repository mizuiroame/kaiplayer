// What was played, newest last: songs (by path) and playlists (by name).
// Kept in history.txt in KaiPlayer's folder for the Home tab.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define HISTORY_MAX 4000  // Older plays are dropped.

typedef enum {
    HISTORY_SONG,
    HISTORY_PLAYLIST,
} HistoryKind;

typedef struct {
    HistoryKind kind;
    char *key;  // Song path or playlist name.
} HistoryEntry;

typedef struct {
    HistoryEntry *items;
    size_t count, capacity;
} History;

void historyInit(History *h);
void historyFree(History *h);
bool historyAdd(History *h, HistoryKind kind, const char *key);
// One "S<tab>path" or "P<tab>name" line per entry.
bool historyLoad(History *h, FILE *f);
bool historySave(const History *h, FILE *f);
// Writes one entry's line (to append to the file).
void historyWriteEntry(FILE *f, HistoryKind kind, const char *key);
