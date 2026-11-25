#include "bookscan.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "booksub.h"
#include "catalog_scan.h"
#include "config.h"
#include "dav.h"
#include "decoder.h"
#include "ffaudio.h"
#include "kfile.h"
#include "subnet.h"
#include "tags.h"
#include "tags_text.h"
#include "util.h"

#define THREAD_STACK_SIZE 0x40000  // A network folder's reads (libsmb2, curl) run on it.
#define THREAD_PRIORITY 0x33       // Below the library's scan.
#define CACHE_NAME "books.cache"
#define CARD_DEPTH 5               // sdmc:/audiobooks/Author/Series/Book/CD 1/file.mp3
#define NETWORK_DEPTH 4
#define CUE_MAX (1u << 20)
#define ID3_HEADER 10
#define WHY_MAX 160

static Mutex g_lock;
static CondVar g_wake;
static Thread g_thread;
static bool g_started;
static volatile bool g_running;
static volatile bool g_cancel;       // Exiting: a request to the server stops waiting.
static bool g_pending;               // Under g_lock: a look was asked for.
static bool g_want_network, g_want_server;
static bool g_busy;
static BookList g_books;             // Published: written by the thread under g_lock.
static unsigned g_generation;

// The bookmarks' thread.
static Thread g_mark_thread;
static bool g_mark_started;
static Mutex g_mark_lock;
static CondVar g_mark_wake;
static bool g_mark_pending;
static char g_mark_location[UTIL_PATH_MAX];
static double g_mark_seconds;

typedef struct {
    char **paths;
    long *sizes;
    size_t count, capacity;
} FileSet;

static bool stopNow(void)
{
    mutexLock(&g_lock);
    bool stop = !g_running || g_pending;
    mutexUnlock(&g_lock);
    return stop;
}

static void fileSetFree(FileSet *s)
{
    for (size_t i = 0; i < s->count; i++)
        free(s->paths[i]);
    free(s->paths);
    free(s->sizes);
    *s = (FileSet){0};
}

static bool fileSetAdd(FileSet *s, const char *path, long size)
{
    if (s->count >= BOOKS_MAX * 50)
        return true;
    if (s->count == s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 64;
        char **paths = realloc(s->paths, capacity * sizeof(*paths));
        if (!paths)
            return false;
        s->paths = paths;
        long *sizes = realloc(s->sizes, capacity * sizeof(*sizes));
        if (!sizes)
            return false;
        s->sizes = sizes;
        s->capacity = capacity;
    }
    char *copy = strdup(path);
    if (!copy)
        return false;
    s->paths[s->count] = copy;
    s->sizes[s->count++] = size;
    return true;
}

static void walkCard(FileSet *s, const char *folder, int depth)
{
    DIR *d = opendir(folder);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) && !stopNow()) {
        char path[UTIL_PATH_MAX];
        if (e->d_name[0] == '.' || !utilJoinPath(path, sizeof(path), folder, e->d_name))
            continue;
        if (utilEntryIsDir(folder, e)) {
            if (depth > 0)
                walkCard(s, path, depth - 1);
        } else if (utilIsAudioFile(e->d_name)) {
            struct stat st;
            fileSetAdd(s, path, stat(path, &st) == 0 ? (long)st.st_size : 0);
        }
    }
    closedir(d);
}

static void walkNetwork(FileSet *s, const char *folder, int depth)
{
    DavEntry *entries = NULL;
    size_t count = 0;
    char why[WHY_MAX];
    if (!kfileList(folder, &entries, &count, why, sizeof(why)))
        return;
    for (size_t i = 0; i < count && !stopNow(); i++) {
        char path[UTIL_PATH_MAX];
        if (entries[i].name[0] == '.' || !utilJoinPath(path, sizeof(path), folder, entries[i].name))
            continue;
        if (entries[i].is_dir && depth > 0)
            walkNetwork(s, path, depth - 1);
        else if (!entries[i].is_dir && utilIsAudioFile(entries[i].name))
            fileSetAdd(s, path, entries[i].size > 0 && entries[i].size < 0x7FFFFFFF ? (long)entries[i].size : 0);
    }
    free(entries);
}

// The network folder's books: in a folder at its top named as books.
static void findNetworkBooks(FileSet *s)
{
    DavEntry *entries = NULL;
    size_t count = 0;
    char why[WHY_MAX];
    if (!kfileList(DAV_ROOT, &entries, &count, why, sizeof(why)))
        return;
    for (size_t i = 0; i < count && !stopNow(); i++) {
        char path[UTIL_PATH_MAX];
        if (entries[i].is_dir && booksIsBookName(entries[i].name) &&
            utilJoinPath(path, sizeof(path), DAV_ROOT, entries[i].name))
            walkNetwork(s, path, NETWORK_DEPTH);
    }
    free(entries);
}

// The library's songs that are books.
static void findLibraryBooks(FileSet *s)
{
    const Catalog *c = catalogLock();
    for (size_t i = 0; c && i < c->song_count; i++) {
        if (booksIsBookPath(c->songs[i].path))
            fileSetAdd(s, c->songs[i].path, c->songs[i].size);
    }
    catalogUnlock();
}

// --- The server's ---

static JsonDoc *ask(const char *method, const char *params, const JsonValue **body)
{
    char why[WHY_MAX];
    if (stopNow())
        return NULL;
    return subCall(NULL, method, params, body, &g_cancel, why, sizeof(why));
}

static void findServerBooks(BookList *out)
{
    BookSubList lists[BOOKSUB_LISTS_MAX];
    size_t list_count = 0;
    const JsonValue *body;
    JsonDoc *d = ask("getGenres", NULL, &body);
    if (d)
        list_count += bookSubGenreLists(body, lists + list_count, BOOKSUB_LISTS_MAX - list_count);
    jsonFree(d);
    d = ask("getMusicFolders", NULL, &body);
    if (d)
        list_count += bookSubFolderLists(body, lists + list_count, BOOKSUB_LISTS_MAX - list_count);
    jsonFree(d);
    if (list_count == 0)
        return;

    static char ids[BOOKSUB_ALBUMS_MAX][SUB_ID_MAX];
    size_t count = 0;
    for (size_t l = 0; l < list_count && count < BOOKSUB_ALBUMS_MAX; l++) {
        for (size_t offset = 0; offset < BOOKSUB_ALBUMS_MAX; offset += BOOKSUB_PAGE) {
            char params[sizeof(lists[0].params) + 64];
            snprintf(params, sizeof(params), "%.*s&size=%d&offset=%zu", (int)sizeof(lists[l].params) - 1, lists[l].params,
                     BOOKSUB_PAGE, offset);
            size_t page = 0;
            d = ask("getAlbumList2", params, &body);
            if (d)
                count = bookSubAlbumIds(body, ids, count, BOOKSUB_ALBUMS_MAX, &page);
            jsonFree(d);
            if (page < BOOKSUB_PAGE)
                break;
        }
    }
    for (size_t i = 0; i < count; i++) {
        char encoded[SUB_ID_MAX], params[SUB_ID_MAX + 8];
        if (!subEncode(ids[i], encoded, sizeof(encoded)))
            continue;
        snprintf(params, sizeof(params), "id=%s", encoded);
        d = ask("getAlbum", params, &body);
        Book b;
        if (d && bookSubAlbum(body, &b) && !bookListTake(out, &b))
            bookFree(&b);
        jsonFree(d);
    }
    d = ask("getBookmarks", NULL, &body);
    if (d)
        bookSubApplyBookmarks(body, out);
    jsonFree(d);
}

// --- Reading a book ---

static double partDuration(const char *path, long size)
{
    Decoder *d = decoderOpen(path);
    double seconds = 0.0;
    if (d) {
        uint64_t frames = decoderTotalFrames(d);
        uint32_t rate = decoderSampleRate(d);
        seconds = frames && rate ? (double)frames / rate : 0.0;
        decoderClose(d);
    }
    if (seconds <= 0.0 && size > 0)  // An MP3 without its length in a header.
        seconds = size / BOOK_GUESS_BYTES_PER_SECOND;
    return seconds;
}

static bool id3Chapters(const char *path, BookChapters *out)
{
    KFile *f = kfileOpen(path, KFILE_PEEK);
    uint8_t head[ID3_HEADER];
    bool found = false;
    if (f && kfileRead(f, head, sizeof(head)) == sizeof(head) && memcmp(head, "ID3", 3) == 0) {
        size_t size = ID3_HEADER + readSyncsafe32(head + 6);
        uint8_t *tag = size <= TAGS_ID3_MAX ? malloc(size) : NULL;
        if (tag) {
            memcpy(tag, head, ID3_HEADER);
            if (kfileRead(f, tag + ID3_HEADER, size - ID3_HEADER) == size - ID3_HEADER)
                found = bookChaptersParseId3(tag, size, out);
            free(tag);
        }
    }
    if (f)
        kfileClose(f);
    return found;
}

// A file's own chapters, or its .cue sheet's (the same name, ".cue").
static bool fileChapters(const char *path, BookChapters *out)
{
    bool found = false;
    if (ffaudioIsFile(path)) {
        KFile *f = kfileOpen(path, KFILE_PEEK);
        found = f && ffaudioReadChapters(f, out);
        if (f)
            kfileClose(f);
    } else if (utilHasExtension(path, "mp3")) {
        found = id3Chapters(path, out);
    }
    if (found || subIsLocation(path))
        return found;
    char cue[UTIL_PATH_MAX];
    snprintf(cue, sizeof(cue), "%s", path);
    char *dot = strrchr(cue, '.');
    if (!dot || strlen(cue) + 4 >= sizeof(cue))
        return false;
    strcpy(dot, ".cue");
    size_t size;
    uint8_t *text = kfileReadAll(cue, CUE_MAX, &size, NULL);
    if (text)
        found = bookChaptersParseCue((const char *)text, size, out);
    free(text);
    return found;
}

// The book's name and author from its first file's tags, else its folders' names.
static void nameBook(Book *b, const TrackTags *t)
{
    if (!b->server) {
        const char *title = t->album[0] ? t->album : b->part_count == 1 ? t->title : "";
        if (title[0])
            snprintf(b->title, sizeof(b->title), "%s", title);
        const char *author = t->album_artist[0] ? t->album_artist : t->artist;
        if (author[0])
            snprintf(b->author, sizeof(b->author), "%s", author);
    }
    // Without one in the tags: the folder above the book, when that one is in a books' folder
    // (sdmc:/audiobooks/<Author>/<Book>).
    char author[UTIL_PATH_MAX], above[UTIL_PATH_MAX];
    snprintf(author, sizeof(author), "%s", b->key);
    if (!b->author[0] && utilParentPath(author)) {
        snprintf(above, sizeof(above), "%s", author);
        if (utilParentPath(above) && booksIsBookName(utilFileName(above)) && !booksIsBookName(utilFileName(author)))
            snprintf(b->author, sizeof(b->author), "%s", utilFileName(author));
    }
}

// Lengths, chapters and names. False if it was stopped part way.
static bool probeBook(Book *b)
{
    size_t n = b->part_count;
    char **titles = calloc(n ? n : 1, sizeof(*titles));
    TrackTags *tags = malloc(sizeof(*tags));
    BookChapters own = {0};
    bool all_own = n > 0, stopped = false;
    for (size_t i = 0; i < n && titles && tags; i++) {
        if (stopNow()) {
            stopped = true;
            break;
        }
        const BookPart *p = &b->parts[i];
        if (!b->server) {
            b->parts[i].duration = partDuration(p->path, p->size);
            if (i == 0 || n > 1) {
                tagsInit(tags);
                tags->skip_picture = true;
                tagsRead(p->path, tags);
                titles[i] = strdup(tags->title);
                if (i == 0)
                    nameBook(b, tags);
                tagsFree(tags);
            }
        }
        if (all_own && (!b->server || n == 1)) {
            BookChapters part = {0};
            if (fileChapters(p->path, &part))
                bookChaptersAppend(&own, &part, bookPartStart(b, i));
            else
                all_own = false;
            bookChaptersFree(&part);
        }
    }
    if (!stopped && titles && tags) {
        if (b->server && n > 1)
            all_own = false;  // Its songs are its chapters, already.
        if (all_own) {
            bookChaptersFree(&b->chapters);
            b->chapters = own;
            own = (BookChapters){0};
        } else if (!b->server || n == 1) {
            bookChaptersFree(&b->chapters);
        }
        if (b->server && n == 1)
            nameBook(b, &(TrackTags){0});
        bookFinishProbe(b, (const char *const *)titles);
    }
    bookChaptersFree(&own);
    for (size_t i = 0; titles && i < n; i++)
        free(titles[i]);
    free(titles);
    free(tags);
    return !stopped;
}

// --- The list ---

static void cachePath(char *out, size_t size)
{
    snprintf(out, size, "%s/%s", configFolder(), CACHE_NAME);
}

static void saveCache(void)
{
    char path[UTIL_PATH_MAX], part[UTIL_PATH_MAX + 8];
    cachePath(path, sizeof(path));
    snprintf(part, sizeof(part), "%s.new", path);
    FILE *f = fopen(part, "w");
    if (!f)
        return;
    bool ok = booksSave(&g_books, f);  // Only this thread writes it: no lock to read it.
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);
        rename(part, path);
    } else {
        remove(part);
    }
}

static void publish(BookList *list)
{
    mutexLock(&g_lock);
    BookList old = g_books;
    g_books = *list;
    *list = (BookList){0};
    g_books.generation = ++g_generation;
    mutexUnlock(&g_lock);
    bookListFree(&old);
}

// What was read before of a book found again with the same files.
static void reuse(Book *b, const BookList *known)
{
    const Book *k = booksFind(known, b->key);
    if (!k || !k->probed || !bookSameParts(b, k) || b->probed)
        return;
    for (size_t i = 0; i < b->part_count; i++)
        b->parts[i].duration = k->parts[i].duration;
    bookChaptersFree(&b->chapters);
    if (!bookChaptersAppend(&b->chapters, &k->chapters, 0.0))
        return;
    if (!b->server) {
        snprintf(b->title, sizeof(b->title), "%s", k->title);
        snprintf(b->author, sizeof(b->author), "%s", k->author);
    }
    b->duration = k->duration;
    b->probed = true;
}

static void lookForBooks(bool network, bool server)
{
    FileSet files = {0};
    walkCard(&files, BOOKSCAN_CARD_FOLDER, CARD_DEPTH);
    findLibraryBooks(&files);
    if (network && !stopNow())
        findNetworkBooks(&files);
    BookList found = {0};
    booksGroup(&found, (const char *const *)files.paths, files.sizes, files.count);
    fileSetFree(&files);
    if (server && !stopNow())
        findServerBooks(&found);
    if (stopNow()) {
        bookListFree(&found);
        return;
    }
    // A server not reached keeps its books from before, as they were.
    for (size_t i = 0; !server && i < g_books.count; i++) {
        Book copy;
        if (g_books.items[i].server && bookCopy(&copy, &g_books.items[i]) && !bookListTake(&found, &copy))
            bookFree(&copy);
    }
    for (size_t i = 0; i < found.count; i++)
        reuse(&found.items[i], &g_books);
    booksSortByTitle(&found);
    publish(&found);

    bool changed = false;
    for (size_t i = 0; i < g_books.count; i++) {
        if (g_books.items[i].probed)
            continue;
        Book work;
        if (!bookCopy(&work, &g_books.items[i]))
            continue;
        if (!probeBook(&work)) {
            bookFree(&work);
            break;
        }
        mutexLock(&g_lock);
        bookFree(&g_books.items[i]);
        g_books.items[i] = work;
        g_books.generation = ++g_generation;
        mutexUnlock(&g_lock);
        changed = true;
    }
    if (changed) {
        mutexLock(&g_lock);
        booksSortByTitle(&g_books);
        g_books.generation = ++g_generation;
        mutexUnlock(&g_lock);
    }
    saveCache();
}

static void loadCache(void)
{
    char path[UTIL_PATH_MAX];
    cachePath(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    BookList list = {0};
    booksLoad(&list, f);
    fclose(f);
    booksSortByTitle(&list);
    publish(&list);
}

static void workerMain(void *arg)
{
    (void)arg;
    loadCache();
    mutexLock(&g_lock);
    while (g_running) {
        if (!g_pending) {
            condvarWait(&g_wake, &g_lock);
            continue;
        }
        g_pending = false;
        bool network = g_want_network, server = g_want_server;
        g_busy = true;
        mutexUnlock(&g_lock);
        lookForBooks(network, server);
        mutexLock(&g_lock);
        g_busy = g_pending;
    }
    mutexUnlock(&g_lock);
}

// --- Bookmarks ---

static void sendBookmark(const char *location, double seconds)
{
    char id[SUB_ID_MAX], params[SUB_ID_MAX + 48];
    if (!bookSubSongId(location, id, sizeof(id)))
        return;
    snprintf(params, sizeof(params), "id=%s&position=%lld", id, (long long)(seconds * 1000.0));
    const JsonValue *body;
    char why[WHY_MAX];
    jsonFree(subCall(NULL, "createBookmark", params, &body, &g_cancel, why, sizeof(why)));
}

static void markMain(void *arg)
{
    (void)arg;
    mutexLock(&g_mark_lock);
    while (g_running) {
        if (!g_mark_pending) {
            condvarWait(&g_mark_wake, &g_mark_lock);
            continue;
        }
        char location[UTIL_PATH_MAX];
        snprintf(location, sizeof(location), "%s", g_mark_location);
        double seconds = g_mark_seconds;
        g_mark_pending = false;
        mutexUnlock(&g_mark_lock);
        sendBookmark(location, seconds);
        mutexLock(&g_mark_lock);
    }
    mutexUnlock(&g_mark_lock);
}

void bookScanSendBookmark(const char *location, double seconds)
{
    if (!g_mark_started || !subIsLocation(location))
        return;
    mutexLock(&g_mark_lock);
    snprintf(g_mark_location, sizeof(g_mark_location), "%s", location);
    g_mark_seconds = seconds > 0.0 ? seconds : 0.0;
    g_mark_pending = true;
    condvarWakeAll(&g_mark_wake);
    mutexUnlock(&g_mark_lock);
}

// --- Start and stop ---

static bool startThread(Thread *t, ThreadFunc main)
{
    if (R_FAILED(threadCreate(t, main, NULL, NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, -2)))
        return false;
    if (R_FAILED(threadStart(t))) {
        threadClose(t);
        return false;
    }
    return true;
}

bool bookScanInit(void)
{
    mutexInit(&g_lock);
    condvarInit(&g_wake);
    mutexInit(&g_mark_lock);
    condvarInit(&g_mark_wake);
    g_running = true;
    g_cancel = false;
    g_started = startThread(&g_thread, workerMain);
    g_mark_started = g_started && startThread(&g_mark_thread, markMain);
    if (!g_started)
        g_running = false;
    return g_started;
}

void bookScanExit(void)
{
    if (g_started) {
        mutexLock(&g_lock);
        g_running = false;
        g_cancel = true;
        condvarWakeAll(&g_wake);
        mutexUnlock(&g_lock);
        mutexLock(&g_mark_lock);
        condvarWakeAll(&g_mark_wake);
        mutexUnlock(&g_mark_lock);
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        if (g_mark_started) {
            threadWaitForExit(&g_mark_thread);
            threadClose(&g_mark_thread);
        }
        g_started = g_mark_started = false;
    }
    bookListFree(&g_books);
}

void bookScanStart(bool network, bool server)
{
    if (!g_started)
        return;
    mutexLock(&g_lock);
    g_pending = true;
    g_busy = true;
    g_want_network = network;
    g_want_server = server;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
}

const BookList *bookScanLock(void)
{
    mutexLock(&g_lock);
    return &g_books;
}

void bookScanUnlock(void)
{
    mutexUnlock(&g_lock);
}

unsigned bookScanGeneration(void)
{
    mutexLock(&g_lock);
    unsigned g = g_generation;
    mutexUnlock(&g_lock);
    return g;
}

bool bookScanBusy(void)
{
    mutexLock(&g_lock);
    bool busy = g_busy;
    mutexUnlock(&g_lock);
    return busy;
}
