#include "bookchap.h"

#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "tags_text.h"

#define ID3_HEADER 10
#define ID3_FLAG_UNSYNC 0x80
#define ID3_FLAG_EXTENDED 0x40
#define CHAP_TIMES 16          // Start and end times, start and end offsets.
#define CUE_FRAMES 75          // A cue sheet's frames per second.
#define SAME_START 0.001       // Chapters this close start at the same time.

void bookChaptersFree(BookChapters *c)
{
    free(c->items);
    *c = (BookChapters){0};
}

bool bookChaptersAdd(BookChapters *c, double start, const char *title)
{
    if (c->count >= BOOK_CHAPTERS_MAX)
        return true;
    if (c->count == c->capacity) {
        size_t capacity = c->capacity ? c->capacity * 2 : 16;
        BookChapter *items = realloc(c->items, capacity * sizeof(*items));
        if (!items)
            return false;
        c->items = items;
        c->capacity = capacity;
    }
    BookChapter *ch = &c->items[c->count++];
    ch->start = start > 0.0 ? start : 0.0;
    snprintf(ch->title, sizeof(ch->title), "%s", title ? title : "");
    return true;
}

bool bookChaptersAppend(BookChapters *c, const BookChapters *src, double offset)
{
    for (size_t i = 0; i < src->count; i++) {
        if (!bookChaptersAdd(c, src->items[i].start + offset, src->items[i].title))
            return false;
    }
    return true;
}

static int compareStart(const void *a, const void *b)
{
    double x = ((const BookChapter *)a)->start, y = ((const BookChapter *)b)->start;
    return x < y ? -1 : x > y ? 1 : 0;
}

void bookChaptersPrepare(BookChapters *c)
{
    if (c->count == 0)
        return;
    // A stable order for equal times: qsort isn't, so equal starts keep the first by dropping the rest.
    qsort(c->items, c->count, sizeof(*c->items), compareStart);
    size_t kept = 1;
    for (size_t i = 1; i < c->count; i++) {
        if (c->items[i].start - c->items[kept - 1].start < SAME_START) {
            if (!c->items[kept - 1].title[0])
                memcpy(c->items[kept - 1].title, c->items[i].title, sizeof(c->items[i].title));
            continue;
        }
        c->items[kept++] = c->items[i];
    }
    c->count = kept;
    for (size_t i = 0; i < c->count; i++) {
        if (!c->items[i].title[0])
            snprintf(c->items[i].title, sizeof(c->items[i].title), T("Chapter %d"), (int)i + 1);
    }
}

size_t bookChapterAt(const BookChapters *c, double seconds)
{
    size_t found = 0;
    for (size_t i = 0; i < c->count && c->items[i].start <= seconds; i++)
        found = i;
    return found;
}

// --- ID3v2 CHAP ---

static uint32_t frameSize(const uint8_t *p, int version)
{
    return version >= 4 ? readSyncsafe32(p) : readBe32(p);
}

// The TIT2 among a CHAP frame's subframes, as UTF-8 in title.
static void chapterTitle(const uint8_t *p, size_t size, int version, char *title, size_t title_size)
{
    title[0] = '\0';
    size_t at = 0;
    while (at + ID3_HEADER <= size) {
        const uint8_t *f = p + at;
        if (f[0] == 0)
            break;
        uint32_t n = frameSize(f + 4, version);
        if (n > size - at - ID3_HEADER)
            break;
        if (memcmp(f, "TIT2", 4) == 0 && n >= 1) {
            textToUtf8(f + ID3_HEADER + 1, n - 1, f[ID3_HEADER], title, title_size);
            return;
        }
        at += ID3_HEADER + n;
    }
}

static bool parseChap(const uint8_t *body, size_t size, int version, BookChapters *out)
{
    const uint8_t *nul = memchr(body, 0, size);  // The element's id, then its times.
    if (!nul)
        return true;
    size_t at = (size_t)(nul - body) + 1;
    if (size - at < CHAP_TIMES)
        return true;
    uint32_t start_ms = readBe32(body + at);
    char title[BOOK_CHAPTER_TITLE];
    chapterTitle(body + at + CHAP_TIMES, size - at - CHAP_TIMES, version, title, sizeof(title));
    return bookChaptersAdd(out, start_ms / 1000.0, title);
}

bool bookChaptersParseId3(const uint8_t *data, size_t size, BookChapters *out)
{
    if (size < ID3_HEADER || memcmp(data, "ID3", 3) != 0)
        return false;
    int version = data[3];
    uint8_t flags = data[5];
    if ((version != 3 && version != 4) || (flags & ID3_FLAG_UNSYNC))
        return false;  // Unsynchronised tags are rare enough to go without chapters.
    size_t end = ID3_HEADER + readSyncsafe32(data + 6);
    end = end > size ? size : end;
    size_t at = ID3_HEADER;
    if (flags & ID3_FLAG_EXTENDED) {
        if (at + 4 > end)
            return false;
        uint32_t ext = readBe32(data + at);
        at += version >= 4 ? readSyncsafe32(data + at) : ext + 4;
    }
    size_t before = out->count;
    while (at + ID3_HEADER <= end) {
        const uint8_t *f = data + at;
        if (f[0] == 0)
            break;  // Padding.
        uint32_t n = frameSize(f + 4, version);
        if (n > end - at - ID3_HEADER)
            break;
        if (memcmp(f, "CHAP", 4) == 0 && !parseChap(f + ID3_HEADER, n, version, out))
            return false;
        at += ID3_HEADER + n;
    }
    return out->count > before;
}

// --- Cue sheets ---

// Skips spaces; returns the rest of the line from there.
static const char *skipSpaces(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static bool startsWord(const char *s, const char *word)
{
    size_t n = strlen(word);
    return strncasecmp(s, word, n) == 0 && (s[n] == ' ' || s[n] == '\t' || s[n] == '\0');
}

// A quoted (or bare) value into out.
static void cueValue(const char *s, char *out, size_t size)
{
    s = skipSpaces(s);
    size_t n;
    if (*s == '"') {
        s++;
        const char *close = strchr(s, '"');
        n = close ? (size_t)(close - s) : strlen(s);
    } else {
        n = strlen(s);
        while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
            n--;
    }
    snprintf(out, size, "%.*s", (int)(n < size ? n : size - 1), s);
}

bool bookChaptersParseCue(const char *text, size_t size, BookChapters *out)
{
    char *copy = malloc(size + 1);
    if (!copy)
        return false;
    memcpy(copy, text, size);
    copy[size] = '\0';
    char *line = copy;
    if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
        line += 3;  // A UTF-8 mark.
    size_t before = out->count;
    int files = 0;
    bool in_track = false, ok = true;
    char title[BOOK_CHAPTER_TITLE] = "";
    while (line && ok) {
        char *next = strpbrk(line, "\r\n");
        if (next)
            *next++ = '\0';
        const char *s = skipSpaces(line);
        if (startsWord(s, "FILE")) {
            files++;
        } else if (startsWord(s, "TRACK")) {
            in_track = true;
            title[0] = '\0';
        } else if (in_track && startsWord(s, "TITLE")) {
            cueValue(s + 5, title, sizeof(title));
        } else if (in_track && startsWord(s, "INDEX")) {
            int number, mm, ss, ff;
            if (sscanf(s + 5, "%d %d:%d:%d", &number, &mm, &ss, &ff) == 4 && number == 1 && mm >= 0 && ss >= 0 &&
                ff >= 0) {
                ok = bookChaptersAdd(out, mm * 60.0 + ss + ff / (double)CUE_FRAMES, title);
                in_track = false;
            }
        }
        while (next && (*next == '\r' || *next == '\n'))
            next++;
        line = next && *next ? next : NULL;
    }
    free(copy);
    if (!ok || files > 1) {
        out->count = before;
        return false;
    }
    return out->count > before;
}
