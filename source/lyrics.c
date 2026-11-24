#include "lyrics.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STAMPS_PER_LINE 32
#define MS_MAX (100 * 60 * 60 * 1000)  // 100 hours: anything later is a broken time.

// Reads a time tag's inside: "mm:ss", "mm:ss.x", "mm:ss.xx", "mm:ss.xxx" or
// "mm:ss:xx". -1 if it isn't one.
static int readTime(const char *s, size_t len)
{
    size_t i = 0;
    long minutes = 0, seconds = 0, fraction = 0, scale = 1;
    int digits = 0;
    while (i < len && isdigit((unsigned char)s[i]) && digits < 5) {
        minutes = minutes * 10 + (s[i++] - '0');
        digits++;
    }
    if (!digits || i >= len || s[i++] != ':')
        return -1;
    digits = 0;
    while (i < len && isdigit((unsigned char)s[i]) && digits < 2) {
        seconds = seconds * 10 + (s[i++] - '0');
        digits++;
    }
    if (!digits || seconds > 59)
        return -1;
    if (i < len && (s[i] == '.' || s[i] == ':')) {
        i++;
        digits = 0;
        while (i < len && isdigit((unsigned char)s[i]) && digits < 3) {
            fraction = fraction * 10 + (s[i++] - '0');
            scale *= 10;
            digits++;
        }
        if (!digits)
            return -1;
    }
    if (i != len)
        return -1;
    long ms = (minutes * 60 + seconds) * 1000 + fraction * 1000 / scale;
    return ms <= MS_MAX ? (int)ms : -1;
}

// Copies text without word times ("<00:12.30>") and the spaces around, into out.
static size_t cleanText(const char *s, size_t len, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '<') {
            const char *end = memchr(s + i, '>', len - i);
            if (end && readTime(s + i + 1, (size_t)(end - s - i - 1)) >= 0) {
                i = (size_t)(end - s);
                continue;
            }
        }
        out[o++] = s[i];
    }
    size_t start = 0;
    while (start < o && (out[start] == ' ' || out[start] == '\t'))
        start++;
    while (o > start && (out[o - 1] == ' ' || out[o - 1] == '\t' || out[o - 1] == '\r'))
        o--;
    memmove(out, out + start, o - start);
    o -= start;
    out[o] = 0;
    return o;
}

static int compareLines(const void *a, const void *b)
{
    const LyricLine *x = a, *y = b;
    if (x->ms != y->ms)
        return x->ms < y->ms ? -1 : 1;
    return x->text < y->text ? -1 : x->text > y->text;  // The order they had in the file.
}

bool lyricsParse(const char *text, size_t size, Lyrics *out)
{
    memset(out, 0, sizeof(*out));
    if (!text || size == 0 || size > LYRICS_FILE_MAX)
        return false;
    if (size >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) {  // A UTF-8 mark at the start.
        text += 3;
        size -= 3;
    }
    char *buffer = malloc(size + 1);
    LyricLine *lines = malloc(LYRICS_LINES_MAX * sizeof(*lines));
    if (!buffer || !lines) {
        free(buffer);
        free(lines);
        return false;
    }
    size_t used = 0;
    int count = 0;
    long offset = 0;
    const char *p = text, *end = text + size;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl ? nl : end;
        int stamps[STAMPS_PER_LINE], n = 0;
        const char *q = p;
        while (q < line_end && *q == '[') {
            const char *close = memchr(q, ']', (size_t)(line_end - q));
            if (!close)
                break;
            size_t len = (size_t)(close - q - 1);
            int ms = readTime(q + 1, len);
            if (ms >= 0) {
                if (n < STAMPS_PER_LINE)
                    stamps[n++] = ms;
            } else if (len > 7 && !strncmp(q + 1, "offset:", 7)) {
                char number[16];
                size_t nlen = len - 7 < sizeof(number) - 1 ? len - 7 : sizeof(number) - 1;
                memcpy(number, q + 8, nlen);
                number[nlen] = 0;
                offset = strtol(number, NULL, 10);
            } else {
                break;  // A tag ([ar:...]) or not a time: the rest of the line isn't timed text.
            }
            q = close + 1;
        }
        if (n > 0) {
            const char *line_text = buffer + used;
            used += cleanText(q, (size_t)(line_end - q), buffer + used) + 1;
            for (int i = 0; i < n && count < LYRICS_LINES_MAX; i++)
                lines[count++] = (LyricLine){stamps[i], line_text};
        }
        p = nl ? nl + 1 : end;
    }
    if (count == 0) {
        free(buffer);
        free(lines);
        return false;
    }
    // A positive offset shows the lines sooner (as the LRC format says).
    for (int i = 0; i < count; i++) {
        long ms = (long)lines[i].ms - offset;
        lines[i].ms = ms < 0 ? 0 : ms > MS_MAX ? MS_MAX : (int)ms;
    }
    qsort(lines, (size_t)count, sizeof(*lines), compareLines);
    out->lines = lines;
    out->count = count;
    out->buffer = buffer;
    return true;
}

void lyricsFree(Lyrics *l)
{
    free(l->lines);
    free(l->buffer);
    memset(l, 0, sizeof(*l));
}

int lyricsLineAt(const Lyrics *l, double seconds)
{
    if (!l || l->count == 0 || !(seconds >= 0))
        return -1;
    double ms = seconds * 1000.0;
    int lo = 0, hi = l->count - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (l->lines[mid].ms <= ms) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return found;
}

bool lyricsPathFor(const char *song, char *out, size_t size)
{
    const char *slash = strrchr(song, '/');
    const char *dot = strrchr(song, '.');
    size_t base = dot && (!slash || dot > slash + 1) ? (size_t)(dot - song) : strlen(song);
    return snprintf(out, size, "%.*s.lrc", (int)base, song) < (int)size;
}
