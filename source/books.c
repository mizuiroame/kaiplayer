#include "books.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define LINE_MAX_BYTES 4096

static const char *const BOOK_NAMES[] = {
    "audiobooks", "audiobook", "hörbücher", "hörbuch", "horbucher", "horbuch", "hoerbuecher", "hoerbuch",
    "audiolibros", "audiolibro", "livresaudio", "livreaudio", "audiolibri", "audiolivros", "audiolivro",
    "オーディオブック", "有声书", "有聲書", "오디오북",
};

static const char *const DISC_WORDS[] = {"cd", "disc", "disk", "part", "teil", "parte", "disco", "partie", "volume", "vol"};

void bookFree(Book *b)
{
    for (size_t i = 0; i < b->part_count; i++)
        free(b->parts[i].path);
    free(b->parts);
    free(b->key);
    bookChaptersFree(&b->chapters);
    memset(b, 0, sizeof(*b));
}

void bookListFree(BookList *l)
{
    for (size_t i = 0; i < l->count; i++)
        bookFree(&l->items[i]);
    free(l->items);
    *l = (BookList){0};
}

bool bookAddPart(Book *b, const char *path, long size, double duration)
{
    if (b->part_count >= BOOK_PARTS_MAX)
        return true;
    BookPart *parts = realloc(b->parts, (b->part_count + 1) * sizeof(*parts));
    if (!parts)
        return false;
    b->parts = parts;
    char *copy = strdup(path);
    if (!copy)
        return false;
    b->parts[b->part_count++] = (BookPart){copy, size, duration};
    return true;
}

bool bookCopy(Book *out, const Book *in)
{
    *out = *in;
    out->key = NULL;
    out->parts = NULL;
    out->part_count = 0;
    out->chapters = (BookChapters){0};
    out->key = strdup(in->key ? in->key : "");
    bool ok = out->key != NULL;
    for (size_t i = 0; ok && i < in->part_count; i++)
        ok = bookAddPart(out, in->parts[i].path, in->parts[i].size, in->parts[i].duration);
    ok = ok && bookChaptersAppend(&out->chapters, &in->chapters, 0.0);
    if (!ok)
        bookFree(out);
    return ok;
}

bool bookListTake(BookList *l, Book *b)
{
    if (l->count >= BOOKS_MAX)
        return false;
    if (l->count == l->capacity) {
        size_t capacity = l->capacity ? l->capacity * 2 : 16;
        Book *items = realloc(l->items, capacity * sizeof(*items));
        if (!items)
            return false;
        l->items = items;
        l->capacity = capacity;
    }
    l->items[l->count++] = *b;
    memset(b, 0, sizeof(*b));
    return true;
}

const Book *booksFind(const BookList *l, const char *key)
{
    for (size_t i = 0; key && i < l->count; i++) {
        if (l->items[i].key && strcmp(l->items[i].key, key) == 0)
            return &l->items[i];
    }
    return NULL;
}

bool bookSameParts(const Book *a, const Book *b)
{
    if (a->part_count != b->part_count)
        return false;
    for (size_t i = 0; i < a->part_count; i++) {
        if (a->parts[i].size != b->parts[i].size || strcmp(a->parts[i].path, b->parts[i].path) != 0)
            return false;
    }
    return true;
}

// The name in lower case (ASCII letters), without spaces, '_', '-' and '.'.
static void squeeze(const char *name, char *out, size_t size)
{
    size_t n = 0;
    for (; *name && n + 1 < size; name++) {
        unsigned char c = (unsigned char)*name;
        if (c == ' ' || c == '_' || c == '-' || c == '.')
            continue;
        out[n++] = c < 0x80 ? (char)tolower(c) : (char)c;
    }
    out[n] = '\0';
}

bool booksIsBookName(const char *name)
{
    char s[BOOK_TEXT_MAX];
    squeeze(name, s, sizeof(s));
    for (size_t i = 0; i < sizeof(BOOK_NAMES) / sizeof(BOOK_NAMES[0]); i++) {
        if (strcmp(s, BOOK_NAMES[i]) == 0)
            return true;
    }
    // "Hörbücher" written with capitals outside ASCII: compare the ASCII letters around them.
    return strncmp(s, "h\xc3\x96rb\xc3\x9c", 7) == 0;
}

bool booksIsDiscFolder(const char *name)
{
    char s[BOOK_TEXT_MAX];
    squeeze(name, s, sizeof(s));
    for (size_t i = 0; i < sizeof(DISC_WORDS) / sizeof(DISC_WORDS[0]); i++) {
        size_t n = strlen(DISC_WORDS[i]);
        if (strncmp(s, DISC_WORDS[i], n) != 0 || !s[n])
            continue;
        const char *p = s + n;
        while (isdigit((unsigned char)*p))
            p++;
        if (!*p)
            return true;
    }
    return false;
}

// A quick look at the text: could one of its folders be named as books?
static bool mayNameBooks(const char *path)
{
    static const char *const PARTS[] = {"audio", "hörb", "horb", "hoerb", "HÖRB", "オーディオ", "有声", "有聲", "오디오"};
    char lower[UTIL_PATH_MAX];
    size_t n = 0;
    for (; path[n] && n + 1 < sizeof(lower); n++)
        lower[n] = (char)tolower((unsigned char)path[n]);
    lower[n] = '\0';
    for (size_t i = 0; i < sizeof(PARTS) / sizeof(PARTS[0]); i++) {
        if (strstr(lower, PARTS[i]) || strstr(path, PARTS[i]))
            return true;
    }
    return false;
}

bool booksIsBookPath(const char *path)
{
    if (utilHasExtension(path, "m4b"))
        return true;
    // A server's locations have no folders ("sub:/<cover>/<id>.mp3"); most paths name no books at all.
    if (strncmp(path, "sub:/", 5) == 0 || !mayNameBooks(path))
        return false;
    char folder[UTIL_PATH_MAX];
    snprintf(folder, sizeof(folder), "%s", path);
    while (utilParentPath(folder)) {
        if (booksIsBookName(utilFileName(folder)))
            return true;
    }
    return false;
}

int booksCompareNatural(const char *a, const char *b)
{
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0')
                a++;
            while (*b == '0')
                b++;
            size_t na = 0, nb = 0;
            while (isdigit((unsigned char)a[na]))
                na++;
            while (isdigit((unsigned char)b[nb]))
                nb++;
            if (na != nb)
                return na < nb ? -1 : 1;
            int d = strncmp(a, b, na);
            if (d)
                return d;
            a += na;
            b += nb;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        a++;
        b++;
    }
    return *a ? 1 : *b ? -1 : 0;
}

typedef struct {
    char *key;
    const char *path;
    long size;
} Grouped;

// The book a file belongs to: itself, or its folder (the one above a disc folder).
static char *bookKeyOf(const char *path)
{
    if (utilHasExtension(path, "m4b"))
        return strdup(path);
    char folder[UTIL_PATH_MAX];
    snprintf(folder, sizeof(folder), "%s", path);
    if (!utilParentPath(folder) || booksIsBookName(utilFileName(folder)))
        return strdup(path);  // Right in the books' folder (or at a root): a book of its own.
    if (booksIsDiscFolder(utilFileName(folder))) {
        char above[UTIL_PATH_MAX];
        snprintf(above, sizeof(above), "%s", folder);
        if (utilParentPath(above) && !booksIsBookName(utilFileName(above)))
            snprintf(folder, sizeof(folder), "%s", above);
    }
    return strdup(folder);
}

static int compareGrouped(const void *x, const void *y)
{
    const Grouped *a = x, *b = y;
    int k = strcmp(a->key, b->key);
    return k ? k : booksCompareNatural(a->path, b->path);
}

// A book's name from its key: the folder's or the file's, without the extension.
static void titleOfKey(const char *key, char *out, size_t size)
{
    snprintf(out, size, "%s", utilFileName(key));
    char *dot = strrchr(out, '.');
    if (dot && dot != out && utilIsAudioFile(utilFileName(key)))
        *dot = '\0';
}

bool booksGroup(BookList *out, const char *const *paths, const long *sizes, size_t count)
{
    Grouped *g = count ? calloc(count, sizeof(*g)) : NULL;
    if (count && !g)
        return false;
    bool ok = true;
    for (size_t i = 0; i < count && ok; i++) {
        g[i] = (Grouped){bookKeyOf(paths[i]), paths[i], sizes ? sizes[i] : 0};
        ok = g[i].key != NULL;
    }
    if (ok)
        qsort(g, count, sizeof(*g), compareGrouped);
    for (size_t i = 0; i < count && ok;) {
        Book b = {0};
        b.key = strdup(g[i].key);
        ok = b.key != NULL;
        if (ok)
            titleOfKey(b.key, b.title, sizeof(b.title));
        size_t j = i;
        for (; j < count && strcmp(g[j].key, g[i].key) == 0 && ok; j++) {
            if (j > i && strcmp(g[j].path, g[j - 1].path) == 0)
                continue;  // The same file twice.
            ok = bookAddPart(&b, g[j].path, g[j].size, 0.0);
        }
        if (ok && !bookListTake(out, &b))
            ok = out->count >= BOOKS_MAX;  // A full list isn't a failure: the rest are left out.
        bookFree(&b);
        i = j;
    }
    for (size_t i = 0; i < count && g; i++)
        free(g[i].key);
    free(g);
    return ok;
}

static int compareTitles(const void *x, const void *y)
{
    const Book *a = x, *b = y;
    int t = booksCompareNatural(a->title, b->title);
    return t ? t : strcmp(a->key, b->key);
}

void booksSortByTitle(BookList *l)
{
    if (l->count > 1)
        qsort(l->items, l->count, sizeof(*l->items), compareTitles);
}

void bookFinishProbe(Book *b, const char *const *part_titles)
{
    b->duration = 0.0;
    for (size_t i = 0; i < b->part_count; i++)
        b->duration += b->parts[i].duration;
    if (b->chapters.count == 0) {
        for (size_t i = 0; i < b->part_count; i++) {
            char name[BOOK_CHAPTER_TITLE];
            if (part_titles && part_titles[i] && part_titles[i][0])
                snprintf(name, sizeof(name), "%s", part_titles[i]);
            else
                titleOfKey(b->parts[i].path, name, sizeof(name));
            if (!bookChaptersAdd(&b->chapters, bookPartStart(b, i), b->part_count > 1 ? name : NULL))
                break;
        }
    }
    bookChaptersPrepare(&b->chapters);
    b->probed = true;
}

double bookPartStart(const Book *b, size_t part)
{
    double t = 0.0;
    for (size_t i = 0; i < part && i < b->part_count; i++)
        t += b->parts[i].duration;
    return t;
}

double bookTimeAt(const Book *b, size_t part, double position)
{
    return bookPartStart(b, part) + (position > 0.0 ? position : 0.0);
}

void bookLocate(const Book *b, double seconds, size_t *part, double *position)
{
    *part = 0;
    *position = seconds > 0.0 ? seconds : 0.0;
    for (size_t i = 0; i < b->part_count; i++) {
        double d = b->parts[i].duration;
        *part = i;
        if (*position < d || i + 1 == b->part_count || d <= 0.0)
            break;
        *position -= d;
    }
    if (b->part_count && b->parts[*part].duration > 0.0 && *position > b->parts[*part].duration)
        *position = b->parts[*part].duration;
}

size_t bookChapterIndex(const Book *b, double seconds)
{
    return bookChapterAt(&b->chapters, seconds);
}

double bookChapterEnd(const Book *b, size_t chapter)
{
    if (chapter + 1 < b->chapters.count)
        return b->chapters.items[chapter + 1].start;
    return b->duration;
}

// --- The cache file ---

// Text kept on one field of a line: tabs and line breaks become spaces.
static void writeField(FILE *f, const char *text)
{
    for (; *text; text++)
        fputc(*text == '\t' || *text == '\n' || *text == '\r' ? ' ' : *text, f);
}

bool booksSave(const BookList *l, FILE *f)
{
    for (size_t i = 0; i < l->count; i++) {
        const Book *b = &l->items[i];
        fprintf(f, "book\t%d\t%d\t", b->probed ? 1 : 0, b->server ? 1 : 0);
        writeField(f, b->key);
        fputs("\ntitle\t", f);
        writeField(f, b->title);
        fputs("\nauthor\t", f);
        writeField(f, b->author);
        fputc('\n', f);
        for (size_t p = 0; p < b->part_count; p++) {
            fprintf(f, "part\t%ld\t%.3f\t", b->parts[p].size, b->parts[p].duration);
            writeField(f, b->parts[p].path);
            fputc('\n', f);
        }
        for (size_t c = 0; c < b->chapters.count; c++) {
            fprintf(f, "chapter\t%.3f\t", b->chapters.items[c].start);
            writeField(f, b->chapters.items[c].title);
            fputc('\n', f);
        }
        fputs("end\n", f);
    }
    return !ferror(f);
}

// The text after the n-th tab of line, or NULL.
static char *field(char *line, int n)
{
    for (int i = 0; i < n && line; i++) {
        line = strchr(line, '\t');
        line = line ? line + 1 : NULL;
    }
    return line;
}

bool booksLoad(BookList *l, FILE *f)
{
    char *line = malloc(LINE_MAX_BYTES);
    if (!line)
        return false;
    Book b = {0};
    bool open = false, ok = true;
    while (ok && fgets(line, LINE_MAX_BYTES, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "book\t", 5) == 0) {
            bookFree(&b);
            char *key = field(line, 3);
            open = key && *key;
            if (open) {
                b.probed = atoi(field(line, 1)) == 1;
                b.server = atoi(field(line, 2)) == 1;
                b.key = strdup(key);
                ok = b.key != NULL;
            }
        } else if (!open) {
            continue;
        } else if (strncmp(line, "title\t", 6) == 0) {
            snprintf(b.title, sizeof(b.title), "%s", line + 6);
        } else if (strncmp(line, "author\t", 7) == 0) {
            snprintf(b.author, sizeof(b.author), "%s", line + 7);
        } else if (strncmp(line, "part\t", 5) == 0) {
            char *path = field(line, 3);
            double duration = strtod(field(line, 2) ? field(line, 2) : "0", NULL);
            if (path && *path)
                ok = bookAddPart(&b, path, strtol(line + 5, NULL, 10), duration >= 0.0 ? duration : 0.0);
        } else if (strncmp(line, "chapter\t", 8) == 0) {
            char *title = field(line, 2);
            ok = bookChaptersAdd(&b.chapters, strtod(line + 8, NULL), title ? title : "");
        } else if (strcmp(line, "end") == 0) {
            open = false;
            if (b.part_count == 0) {
                bookFree(&b);
                continue;
            }
            b.duration = 0.0;
            for (size_t i = 0; i < b.part_count; i++)
                b.duration += b.parts[i].duration;
            if (b.probed && b.chapters.count == 0)
                b.probed = false;  // Read again.
            if (!bookListTake(l, &b))
                bookFree(&b);
        }
    }
    bookFree(&b);
    free(line);
    return ok;
}
