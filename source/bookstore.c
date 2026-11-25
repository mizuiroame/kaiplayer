#include "bookstore.h"

#include <stdlib.h>
#include <string.h>

#define LINE_MAX_BYTES 2048
#define POSITION_MAX (7.0 * 24 * 3600)  // A week into one file is no position.

void bookStoreFree(BookStore *s)
{
    for (size_t i = 0; i < s->count; i++)
        free(s->items[i].key);
    free(s->items);
    free(s->active);
    *s = (BookStore){0};
}

const BookMark *bookStoreFind(const BookStore *s, const char *key)
{
    for (size_t i = 0; key && i < s->count; i++) {
        if (strcmp(s->items[i].key, key) == 0)
            return &s->items[i];
    }
    return NULL;
}

bool bookStoreSet(BookStore *s, const char *key, size_t part, double position, long long now, bool finished)
{
    if (!key || !key[0] || strchr(key, '\n'))
        return false;
    if (!(position >= 0.0) || position > POSITION_MAX)
        position = 0.0;
    BookMark *m = (BookMark *)bookStoreFind(s, key);
    if (!m) {
        if (s->count >= BOOKSTORE_MAX) {  // The oldest goes.
            size_t oldest = 0;
            for (size_t i = 1; i < s->count; i++)
                if (s->items[i].updated < s->items[oldest].updated)
                    oldest = i;
            free(s->items[oldest].key);
            s->items[oldest] = s->items[--s->count];
        }
        if (s->count == s->capacity) {
            size_t capacity = s->capacity ? s->capacity * 2 : 16;
            BookMark *items = realloc(s->items, capacity * sizeof(*items));
            if (!items)
                return false;
            s->items = items;
            s->capacity = capacity;
        }
        char *copy = strdup(key);
        if (!copy)
            return false;
        m = &s->items[s->count++];
        m->key = copy;
    }
    m->part = part;
    m->position = position;
    m->updated = now;
    m->finished = finished;
    return true;
}

void bookStoreForget(BookStore *s, const char *key)
{
    BookMark *m = (BookMark *)bookStoreFind(s, key);
    if (!m)
        return;
    free(m->key);
    *m = s->items[--s->count];
}

bool bookStoreSetActive(BookStore *s, const char *key)
{
    free(s->active);
    s->active = key && key[0] ? strdup(key) : NULL;
    return !key || !key[0] || s->active;
}

bool bookStoreSave(const BookStore *s, FILE *f)
{
    if (s->speed)
        fprintf(f, "speed\t%d\n", s->speed);
    if (s->active)
        fprintf(f, "active\t%s\n", s->active);
    for (size_t i = 0; i < s->count; i++) {
        const BookMark *m = &s->items[i];
        fprintf(f, "mark\t%zu\t%.2f\t%lld\t%d\t%s\n", m->part, m->position, m->updated, m->finished ? 1 : 0, m->key);
    }
    return !ferror(f);
}

bool bookStoreLoad(BookStore *s, FILE *f)
{
    char *line = malloc(LINE_MAX_BYTES);
    if (!line)
        return false;
    bool ok = true;
    while (ok && fgets(line, LINE_MAX_BYTES, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "speed\t", 6) == 0) {
            s->speed = atoi(line + 6);
        } else if (strncmp(line, "active\t", 7) == 0) {
            ok = bookStoreSetActive(s, line + 7);
        } else if (strncmp(line, "mark\t", 5) == 0) {
            char *p = line + 5, *end;
            unsigned long part = strtoul(p, &end, 10);
            if (*end != '\t')
                continue;
            double position = strtod(end + 1, &end);
            if (*end != '\t')
                continue;
            long long updated = strtoll(end + 1, &end, 10);
            if (*end != '\t')
                continue;
            int finished = (int)strtol(end + 1, &end, 10);
            if (*end != '\t' || !end[1])
                continue;
            ok = bookStoreSet(s, end + 1, part, position, updated, finished == 1);
        }
    }
    free(line);
    return ok;
}
