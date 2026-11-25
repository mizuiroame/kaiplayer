// The Audiobooks tab and the book playing: a book plays its files in order
// at its own speed, goes on where it was left (books.txt, apart from the
// music's resume), moves by chapters and 30 seconds, and can pause at the end
// of a chapter. A Navidrome book also tells the server where it was left.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "app.h"
#include "app_buttons.h"
#include "applog.h"
#include "bookscan.h"
#include "catalog_scan.h"
#include "config.h"
#include "dav.h"
#include "lang.h"
#include "player.h"
#include "tempo.h"
#include "ui.h"
#include "util.h"

#define NS_PER_SECOND 1000000000ull
#define SAVE_INTERVAL_NS (15 * NS_PER_SECOND)   // The place kept while playing.
#define MARK_INTERVAL_NS (60 * NS_PER_SECOND)   // And told to the server.
#define SKIP_SECONDS 30.0
#define MUSIC_SEEK_SECONDS 10.0
#define CHAPTER_RESTART_SECONDS 3.0             // Further into a chapter, ⏮ goes to its start.
#define END_SECONDS 5.0                         // A book stopped this near its end was finished.
#define SLEEP_FADE_SECONDS 2.0
#define STARTED_SECONDS 1.0                     // Less than this in: not started.
#define MENU_TITLE_CHARS 40

enum {
    ACT_CHAPTER = APP_BOOK_ACTIONS,  // arg: the chapter.
    ACT_SPEED,                       // arg: thousandths.
    ACT_SLEEP,                       // arg: minutes; 0 off; -1 the chapter's end.
    ACT_PLAY,
    ACT_CHAPTERS,
    ACT_START_OVER,
    ACT_FINISHED,
    ACT_NOT_FINISHED,
    ACT_RESCAN,
    ACT_SPEED_MENU,
    ACT_SLEEP_MENU,
};

static const int SLEEP_MINUTES[] = {15, 30, 45, 60, 90};

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

static long long nowSeconds(void)
{
    return (long long)time(NULL);
}

// --- Where books were left ---

static void storePath(char *out, size_t size)
{
    snprintf(out, size, "%s/%s", configFolder(), BOOKSTORE_FILE);
}

static void saveStore(App *app)
{
    char path[UTIL_PATH_MAX], part[UTIL_PATH_MAX + 8];
    storePath(path, sizeof(path));
    snprintf(part, sizeof(part), "%s.new", path);
    FILE *f = fopen(part, "w");
    if (!f)
        return;
    bool ok = bookStoreSave(&app->books.store, f);
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);
        rename(part, path);
    } else {
        remove(part);
    }
}

static void loadStore(App *app)
{
    char path[UTIL_PATH_MAX];
    storePath(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f) {
        bookStoreLoad(&app->books.store, f);
        fclose(f);
    }
}

static int bookSpeed(const App *app)
{
    int speed = app->books.store.speed;
    return speed >= TEMPO_MIN && speed <= TEMPO_MAX ? speed : TEMPO_PERMILLE;
}

const BookMark *appBooksMark(const App *app, const char *key)
{
    return bookStoreFind(&app->books.store, key);
}

bool appBookPlaying(const App *app, const PlayerStatus *status)
{
    return app->books.active && status->book && status->generation == app->books.generation;
}

// Keeps where the book playing is (or that it was finished), and tells its server when tell.
static void keepPlace(App *app, const PlayerStatus *s, bool finished, bool tell)
{
    BooksTab *t = &app->books;
    if (!appBookPlaying(app, s))
        return;
    size_t part = finished ? 0 : s->track;
    double position = finished ? 0.0 : s->position;
    bookStoreSet(&t->store, t->book.key, part, position, nowSeconds(), finished);
    t->order_stale = true;
    saveStore(app);
    if (tell && t->book.server && part < t->book.part_count)
        bookScanSendBookmark(t->book.parts[part].path, position);
}

// --- The shown order: going on first (latest first), then new, then finished ---

static int groupOf(const App *app, const Book *b)
{
    const BookMark *m = bookStoreFind(&app->books.store, b->key);
    if (m && m->finished)
        return 2;
    if (m && (m->part > 0 || m->position >= STARTED_SECONDS))
        return 0;
    return 1;
}

static const App *g_sort_app;
static const BookList *g_sort_list;

static int compareShown(const void *x, const void *y)
{
    const Book *a = &g_sort_list->items[*(const size_t *)x], *b = &g_sort_list->items[*(const size_t *)y];
    int ga = groupOf(g_sort_app, a), gb = groupOf(g_sort_app, b);
    if (ga != gb)
        return ga - gb;
    if (ga == 0) {
        long long ua = bookStoreFind(&g_sort_app->books.store, a->key)->updated;
        long long ub = bookStoreFind(&g_sort_app->books.store, b->key)->updated;
        if (ua != ub)
            return ua > ub ? -1 : 1;
    }
    size_t ia = *(const size_t *)x, ib = *(const size_t *)y;
    return ia < ib ? -1 : ia > ib;  // The list's order: by title.
}

static void rebuildOrder(App *app, const BookList *l)
{
    BooksTab *t = &app->books;
    char key[UTIL_PATH_MAX] = "";
    const Book *at = appBooksShown(app, l, t->cursor);
    if (at)
        snprintf(key, sizeof(key), "%s", at->key);
    else if (t->cursor < t->order_count && t->order_generation != l->generation)
        key[0] = '\0';
    size_t *order = l->count ? malloc(l->count * sizeof(*order)) : NULL;
    if (l->count && !order)
        return;
    for (size_t i = 0; i < l->count; i++)
        order[i] = i;
    g_sort_app = app;
    g_sort_list = l;
    if (l->count > 1)
        qsort(order, l->count, sizeof(*order), compareShown);
    free(t->order);
    t->order = order;
    t->order_count = l->count;
    t->order_generation = l->generation;
    t->order_stale = false;
    for (size_t i = 0; key[0] && i < l->count; i++) {
        if (strcmp(l->items[order[i]].key, key) == 0)
            t->cursor = i;
    }
    if (t->cursor >= t->order_count)
        t->cursor = t->order_count ? t->order_count - 1 : 0;
}

size_t appBooksCount(const App *app)
{
    return app->books.order_count;
}

const Book *appBooksShown(const App *app, const BookList *list, size_t row)
{
    const BooksTab *t = &app->books;
    if (!list || list->generation != t->order_generation || row >= t->order_count || t->order[row] >= list->count)
        return NULL;
    return &list->items[t->order[row]];
}

// A copy of the book with this key, or of the one at row (key NULL). False if it's gone.
static bool copyBook(App *app, const char *key, size_t row, Book *out)
{
    const BookList *l = bookScanLock();
    const Book *b = key ? booksFind(l, key) : appBooksShown(app, l, row);
    bool ok = b && bookCopy(out, b);
    bookScanUnlock();
    return ok;
}

// --- Playing ---

void appBooksStop(App *app)
{
    BooksTab *t = &app->books;
    if (!t->active)
        return;
    PlayerStatus s;
    playerGetStatus(&s);
    keepPlace(app, &s, false, true);
    t->active = false;
    t->sleep_chapter = false;
    bookFree(&t->book);
    bookStoreSetActive(&t->store, NULL);
    saveStore(app);
}

// Plays b from seconds into it, or (seconds < 0) from where it was left, on this console or its server.
static bool startBook(App *app, const Book *b, double seconds, bool play)
{
    BooksTab *t = &app->books;
    if (b->part_count == 0)
        return false;
    for (size_t i = 0; i < b->part_count; i++) {  // A network folder's book: its password first, if it's asked for.
        if (davIsLocation(b->parts[i].path)) {
            if (!appNetworkEnsurePassword(app))
                return false;
            break;
        }
    }
    Book copy;
    TrackList list = {0};
    bool ok = bookCopy(&copy, b);
    for (size_t i = 0; ok && i < copy.part_count; i++)
        ok = trackListAdd(&list, copy.parts[i].path);
    if (!ok) {
        trackListFree(&list);
        bookFree(&copy);
        appNotify(app, NOTICE_ERROR, T("Could not open the audiobook."));
        return false;
    }
    size_t part = 0;
    double position = 0.0;
    const BookMark *m = bookStoreFind(&t->store, copy.key);
    if (seconds >= 0.0) {
        bookLocate(&copy, seconds, &part, &position);
    } else {
        if (m && !m->finished) {
            part = m->part;
            position = m->position;
        }
        if (copy.mark_time && (!m || copy.mark_time > m->updated)) {  // Left later on another player.
            part = copy.mark_part;
            position = copy.mark_position;
        }
        if (part >= copy.part_count) {
            part = 0;
            position = 0.0;
        }
    }
    appBooksStop(app);
    t->book = copy;
    t->active = true;
    app->playing_playlist[0] = '\0';
    playerPlayBook(&list, part, position, bookSpeed(app), play);
    trackListFree(&list);
    PlayerStatus s;
    playerGetStatus(&s);
    t->generation = s.generation;
    t->last_state = s.state;
    t->last_part = part;
    t->last_position = position;
    t->next_save_ns = nowNs() + SAVE_INTERVAL_NS;
    t->next_mark_ns = nowNs() + MARK_INTERVAL_NS;
    bookStoreSet(&t->store, copy.key, part, position, nowSeconds(), false);
    bookStoreSetActive(&t->store, copy.key);
    t->order_stale = true;
    saveStore(app);
    return true;
}

void appBooksPlayRow(App *app, size_t row)
{
    PlayerStatus s;
    playerGetStatus(&s);
    Book b;
    if (!copyBook(app, NULL, row, &b))
        return;
    if (appBookPlaying(app, &s) && strcmp(b.key, app->books.book.key) == 0) {
        if (s.state != PLAYER_PLAYING)  // The one playing: it goes on.
            playerTogglePause();
    } else {
        startBook(app, &b, -1.0, true);
    }
    bookFree(&b);
    appSetView(app, VIEW_NOW_PLAYING);
}

// The book playing, at seconds into it.
static void goToBookTime(App *app, double seconds)
{
    BooksTab *t = &app->books;
    PlayerStatus s;
    playerGetStatus(&s);
    if (!appBookPlaying(app, &s))
        return;
    if (t->book.duration > 0.0 && seconds > t->book.duration - 1.0)
        seconds = t->book.duration - 1.0;
    size_t part;
    double position;
    bookLocate(&t->book, seconds, &part, &position);
    if (part == s.track && s.state != PLAYER_STOPPED)
        playerSeekTo(position);
    else
        playerJumpToAt(part, position);
    t->last_part = part;
    t->last_position = position;
}

static double bookTimeOf(const App *app, const PlayerStatus *s)
{
    return bookTimeAt(&app->books.book, s->track, s->position);
}

void appTransportPrev(App *app)
{
    PlayerStatus s;
    playerGetStatus(&s);
    if (!appBookPlaying(app, &s)) {
        playerPrev();
        return;
    }
    const Book *b = &app->books.book;
    double now = bookTimeOf(app, &s);
    size_t ch = bookChapterIndex(b, now);
    if (b->chapters.count == 0)
        return;
    double start = b->chapters.items[ch].start;
    if (now - start < CHAPTER_RESTART_SECONDS && ch > 0)
        start = b->chapters.items[ch - 1].start;
    goToBookTime(app, start);
}

void appTransportNext(App *app)
{
    PlayerStatus s;
    playerGetStatus(&s);
    if (!appBookPlaying(app, &s)) {
        playerNext();
        return;
    }
    const Book *b = &app->books.book;
    size_t ch = bookChapterIndex(b, bookTimeOf(app, &s));
    if (ch + 1 < b->chapters.count)
        goToBookTime(app, b->chapters.items[ch + 1].start);
}

void appTransportSeek(App *app, int direction)
{
    PlayerStatus s;
    playerGetStatus(&s);
    if (!appBookPlaying(app, &s)) {
        playerSeek(direction * MUSIC_SEEK_SECONDS);
        return;
    }
    double to = bookTimeOf(app, &s) + direction * SKIP_SECONDS;
    goToBookTime(app, to < 0.0 ? 0.0 : to);
}

void appBookSeekChapter(App *app, double fraction)
{
    PlayerStatus s;
    playerGetStatus(&s);
    if (!appBookPlaying(app, &s) || app->books.book.chapters.count == 0)
        return;
    const Book *b = &app->books.book;
    size_t ch = bookChapterIndex(b, bookTimeOf(app, &s));
    double start = b->chapters.items[ch].start, end = bookChapterEnd(b, ch);
    fraction = fraction < 0.0 ? 0.0 : fraction > 1.0 ? 1.0 : fraction;
    goToBookTime(app, start + (end - start) * fraction);
}

// --- Start, end, each frame ---

void appBooksStart(App *app)
{
    loadStore(app);
    appLog("books: start, %zu places kept%s", app->books.store.count, app->books.store.active ? ", a book open" : "");
    app->books.menu_playing = true;
    bookScanInit();
    bookScanStart(appNetworkReady(), appServerReady());
}

void appBooksRescan(App *app)
{
    (void)app;
    bookScanStart(appNetworkReady(), appServerReady());
}

bool appBooksWillRestore(const App *app, bool play)
{
    BooksTab *t = (BooksTab *)&app->books;
    if (!t->store.active)
        return false;
    snprintf(t->restore, sizeof(t->restore), "%s", t->store.active);
    t->restore_play = play;
    return true;
}

void appBooksExit(App *app)
{
    BooksTab *t = &app->books;
    PlayerStatus s;
    playerGetStatus(&s);
    if (appBookPlaying(app, &s)) {
        keepPlace(app, &s, false, true);
        bookStoreSetActive(&t->store, t->book.key);
    } else {
        bookStoreSetActive(&t->store, NULL);
    }
    saveStore(app);
    bookScanExit();
    bookStoreFree(&t->store);
    bookFree(&t->book);
    free(t->order);
    t->order = NULL;
    t->order_count = 0;
}

// The book playing when KaiPlayer closed, opened once the list has it; the music's queue if it's gone.
static void tickRestore(App *app)
{
    BooksTab *t = &app->books;
    if (!t->restore[0])
        return;
    Book b;
    if (copyBook(app, t->restore, 0, &b)) {
        appLog("books: opening the book left open");
        t->restore[0] = '\0';
        if (startBook(app, &b, -1.0, t->restore_play))
            app->view = VIEW_NOW_PLAYING;
        bookFree(&b);
    } else if (!bookScanBusy()) {
        appLog("books: the book left open is gone: the music's queue instead");
        t->restore[0] = '\0';
        appRestoreSession(app, t->restore_play);
    }
}

// The book's end: kept as finished, and said.
static void tickFinished(App *app, const PlayerStatus *s)
{
    BooksTab *t = &app->books;
    if (s->state == PLAYER_STOPPED && t->last_state == PLAYER_PLAYING && t->last_part + 1 >= t->book.part_count) {
        double length = t->book.parts[t->last_part].duration;
        if (length <= 0.0 || t->last_position >= length - END_SECONDS) {
            keepPlace(app, s, true, true);
            char message[BOOK_TEXT_MAX + 64];
            snprintf(message, sizeof(message), T("Finished \"%s\"."), t->book.title);
            appToast(app, message);
        }
    }
}

static void tickSleepChapter(App *app, const PlayerStatus *s)
{
    BooksTab *t = &app->books;
    if (!t->sleep_chapter || s->state != PLAYER_PLAYING)
        return;
    double now = bookTimeOf(app, s);
    size_t ch = bookChapterIndex(&t->book, now);
    double fade = SLEEP_FADE_SECONDS * s->speed / TEMPO_PERMILLE;
    // The chapter's end reached (or just passed): pause. Moved to another chapter: its end instead.
    bool at_end = ch == t->sleep_at && now >= bookChapterEnd(&t->book, ch) - fade;
    bool just_past = ch == t->sleep_at + 1 && t->book.chapters.count > ch &&
                     now - t->book.chapters.items[ch].start < CHAPTER_RESTART_SECONDS;
    if (!at_end && !just_past && ch != t->sleep_at)
        t->sleep_at = ch;
    if (at_end || just_past) {
        t->sleep_chapter = false;
        playerFadeOutAndPause(SLEEP_FADE_SECONDS);
        appToast(app, T("Sleep timer: pausing."));
    }
}

void appBooksTick(App *app, const PlayerStatus *status, uint64_t now_ns)
{
    BooksTab *t = &app->books;
    // The library changed (scanned, or another source): its books are looked for again.
    const Catalog *c = catalogLock();
    unsigned generation = c ? c->generation : 0;
    catalogUnlock();
    size_t done, total;
    if (generation != t->catalog_generation && !catalogScanProgress(&done, &total)) {
        appLog("books: the library changed (%u): looking again", generation);
        t->catalog_generation = generation;
        appBooksRescan(app);
    }
    const BookList *l = bookScanLock();
    if (l->generation != t->order_generation || t->order_stale)
        rebuildOrder(app, l);
    bookScanUnlock();
    tickRestore(app);

    if (t->active && !appBookPlaying(app, status)) {  // Music took over (its place was kept before).
        t->active = false;
        t->sleep_chapter = false;
        bookFree(&t->book);
        bookStoreSetActive(&t->store, NULL);
        saveStore(app);
        return;
    }
    if (!t->active)
        return;
    tickFinished(app, status);
    tickSleepChapter(app, status);
    bool paused = status->state != PLAYER_PLAYING && t->last_state == PLAYER_PLAYING;
    if (status->state != PLAYER_STOPPED) {
        if (paused || (status->state == PLAYER_PLAYING && now_ns >= t->next_save_ns)) {
            bool tell = paused || now_ns >= t->next_mark_ns;
            keepPlace(app, status, false, tell);
            t->next_save_ns = now_ns + SAVE_INTERVAL_NS;
            if (tell)
                t->next_mark_ns = now_ns + MARK_INTERVAL_NS;
        }
        t->last_part = status->track;
        t->last_position = status->position;
    }
    t->last_state = status->state;
}

// --- Buttons ---

static void moveCursor(App *app, long delta)
{
    BooksTab *t = &app->books;
    if (t->order_count == 0)
        return;
    long next = (long)t->cursor + delta;
    next = next < 0 ? 0 : next >= (long)t->order_count ? (long)t->order_count - 1 : next;
    t->cursor = (size_t)next;
}

void appHandleBooks(App *app, uint64_t pressed)
{
    BooksTab *t = &app->books;
    pressed = appFocusFilter(app, pressed, t->cursor, t->order_count, false, false);
    if (pressed & BUTTONS_UP) moveCursor(app, -1);
    if (pressed & BUTTONS_DOWN) moveCursor(app, 1);
    if (pressed & BUTTONS_PAGE_UP) moveCursor(app, -UI_LIST_ROWS);
    if (pressed & BUTTONS_PAGE_DOWN) moveCursor(app, UI_LIST_ROWS);
    if (t->cursor >= t->order_count)
        return;
    if (pressed & HidNpadButton_A)
        appBooksPlayRow(app, t->cursor);
    else if (pressed & HidNpadButton_X)
        appBooksOpenChapters(app, (int)t->cursor);
    else if (pressed & HidNpadButton_Y)
        appBooksOpenOptions(app, (int)t->cursor);
}

void appHandleBookPlayer(App *app, uint64_t pressed)
{
    if (pressed & HidNpadButton_A) playerTogglePause();
    if (pressed & HidNpadButton_B) appSetView(app, VIEW_BOOKS);
    if (pressed & HidNpadButton_Left) appTransportSeek(app, -1);
    if (pressed & HidNpadButton_Right) appTransportSeek(app, 1);
    if (pressed & HidNpadButton_X) appBooksOpenChapters(app, -1);
    else if (pressed & HidNpadButton_Y) appBooksOpenSpeed(app);
    else if (pressed & HidNpadButton_Up) appBooksOpenSleep(app);
}

// --- Menus ---

// A book's title cut to fit a menu's heading or item, whole characters only.
static void shortTitle(const char *title, char *out, size_t size)
{
    size_t n = 0, chars = 0;
    while (title[n] && chars < MENU_TITLE_CHARS && n + 4 < size) {
        unsigned char c = (unsigned char)title[n];
        size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        n += len;
        chars++;
    }
    snprintf(out, size, "%.*s%s", (int)n, title, title[n] ? "..." : "");
}

// The book a menu acts on: a copy. False if it's gone.
static bool menuBook(App *app, Book *out)
{
    BooksTab *t = &app->books;
    if (t->menu_playing) {
        PlayerStatus s;
        playerGetStatus(&s);
        return appBookPlaying(app, &s) && bookCopy(out, &t->book);
    }
    return copyBook(app, t->menu_key, 0, out);
}

// The menus act on the book playing (row -1) or the one at row.
static bool aimMenu(App *app, int row)
{
    BooksTab *t = &app->books;
    t->menu_playing = row < 0;
    if (row < 0)
        return true;
    Book b;
    if (!copyBook(app, NULL, (size_t)row, &b))
        return false;
    snprintf(t->menu_key, sizeof(t->menu_key), "%s", b.key);
    bookFree(&b);
    return true;
}

// The chapters of the book the menus aim at.
static void openChaptersMenu(App *app)
{
    Book b;
    if (!menuBook(app, &b))
        return;
    char title[BOOK_TEXT_MAX];
    shortTitle(b.title, title, sizeof(title));
    menuOpen(&app->menu, title);
    // The chapter playing, or where the book was left.
    double at = 0.0;
    PlayerStatus s;
    playerGetStatus(&s);
    const BookMark *m = bookStoreFind(&app->books.store, b.key);
    if (app->books.menu_playing)
        at = bookTimeOf(app, &s);
    else if (m && !m->finished && b.probed)
        at = bookTimeAt(&b, m->part, m->position);
    size_t current = bookChapterIndex(&b, at);
    for (size_t i = 0; i < b.chapters.count; i++) {
        char label[MENU_LABEL_MAX], time_text[16];
        snprintf(label, sizeof(label), "%zu.  %s", i + 1, b.chapters.items[i].title);
        utilFormatTime(time_text, sizeof(time_text), b.chapters.items[i].start);
        if (!menuAdd(&app->menu, label, ACT_CHAPTER, (int)i))
            break;
        menuMarkLast(&app->menu, b.probed ? time_text : "", i == current && (m || app->books.menu_playing));
    }
    if (b.chapters.count == 0)
        menuAdd(&app->menu, T("Reading the book..."), ACT_PLAY, 0);
    app->menu.cursor = (int)(current < (size_t)app->menu.count ? current : 0);
    bookFree(&b);
}

void appBooksOpenChapters(App *app, int row)
{
    if (aimMenu(app, row))
        openChaptersMenu(app);
}

void appBooksOpenSpeed(App *app)
{
    menuOpen(&app->menu, T("Reading speed"));
    int speed = bookSpeed(app);
    for (int i = 0; i < TEMPO_SPEED_COUNT; i++) {
        char label[32];
        int permille = tempoSpeedPermille(i);
        tempoFormatSpeed(label, sizeof(label), permille);
        menuAdd(&app->menu, label, ACT_SPEED, permille);
        menuMarkLast(&app->menu, permille == TEMPO_PERMILLE ? T("Normal") : "", permille == speed);
        if (permille == speed)
            app->menu.cursor = i;
    }
}

void appBooksOpenSleep(App *app)
{
    menuOpen(&app->menu, T("Sleep timer"));
    menuAdd(&app->menu, T("Off"), ACT_SLEEP, 0);
    menuMarkLast(&app->menu, "", !app->sleep_deadline_ns && !app->books.sleep_chapter);
    for (size_t i = 0; i < sizeof(SLEEP_MINUTES) / sizeof(SLEEP_MINUTES[0]); i++) {
        char label[48];
        snprintf(label, sizeof(label), T("%d minutes"), SLEEP_MINUTES[i]);
        menuAdd(&app->menu, label, ACT_SLEEP, SLEEP_MINUTES[i]);
        menuMarkLast(&app->menu, "", app->sleep_deadline_ns && app->sleep_minutes == SLEEP_MINUTES[i]);
    }
    menuAdd(&app->menu, T("End of chapter"), ACT_SLEEP, -1);
    menuMarkLast(&app->menu, "", app->books.sleep_chapter);
    for (int i = 0; i < app->menu.count; i++)
        if (app->menu.items[i].checked)
            app->menu.cursor = i;
}

void appBooksOpenOptions(App *app, int row)
{
    Book b;
    if (!aimMenu(app, row) || !menuBook(app, &b))
        return;
    char title[BOOK_TEXT_MAX];
    shortTitle(b.title, title, sizeof(title));
    menuOpen(&app->menu, title);
    const BookMark *m = bookStoreFind(&app->books.store, b.key);
    menuAdd(&app->menu, m && !m->finished ? T("Continue") : T("Play"), ACT_PLAY, 0);
    menuAdd(&app->menu, T("Chapters"), ACT_CHAPTERS, 0);
    menuAdd(&app->menu, T("Start from the beginning"), ACT_START_OVER, 0);
    if (m && m->finished)
        menuAdd(&app->menu, T("Mark as not finished"), ACT_NOT_FINISHED, 0);
    else
        menuAdd(&app->menu, T("Mark as finished"), ACT_FINISHED, 0);
    bookFree(&b);
}

void appBooksMenuItems(App *app)
{
    PlayerStatus s;
    playerGetStatus(&s);
    BooksTab *t = &app->books;
    if (app->view == VIEW_NOW_PLAYING && appBookPlaying(app, &s)) {
        t->menu_playing = true;
        menuAdd(&app->menu, T("Chapters"), ACT_CHAPTERS, 0);
        menuAdd(&app->menu, T("Reading speed"), ACT_SPEED_MENU, 0);
        menuAdd(&app->menu, T("Sleep timer"), ACT_SLEEP_MENU, 0);
        menuAdd(&app->menu, T("Mark as finished"), ACT_FINISHED, 0);
        return;
    }
    if (app->view != VIEW_BOOKS)
        return;
    if (t->cursor < t->order_count && aimMenu(app, (int)t->cursor)) {
        Book b;
        if (menuBook(app, &b)) {
            char title[BOOK_TEXT_MAX], label[MENU_LABEL_MAX];
            shortTitle(b.title, title, sizeof(title));
            snprintf(label, sizeof(label), T("Chapters of \"%s\""), title);
            menuAdd(&app->menu, label, ACT_CHAPTERS, 0);
            snprintf(label, sizeof(label), T("Start \"%s\" over"), title);
            menuAdd(&app->menu, label, ACT_START_OVER, 0);
            const BookMark *m = bookStoreFind(&t->store, b.key);
            snprintf(label, sizeof(label), m && m->finished ? T("Mark \"%s\" as not finished") : T("Mark \"%s\" as finished"),
                     title);
            menuAdd(&app->menu, label, m && m->finished ? ACT_NOT_FINISHED : ACT_FINISHED, 0);
            bookFree(&b);
        }
    }
    menuAdd(&app->menu, T("Look for audiobooks again"), ACT_RESCAN, 0);
}

static bool isPlaying(App *app, const Book *b)
{
    PlayerStatus s;
    playerGetStatus(&s);
    return appBookPlaying(app, &s) && strcmp(app->books.book.key, b->key) == 0;
}

static void startAt(App *app, const Book *b, double seconds)
{
    if (isPlaying(app, b))
        goToBookTime(app, seconds);
    else
        startBook(app, b, seconds, true);
    appSetView(app, VIEW_NOW_PLAYING);
}

void appBooksMenuAction(App *app, int action, int arg)
{
    BooksTab *t = &app->books;
    if (action == ACT_SPEED) {
        t->store.speed = arg;
        saveStore(app);
        playerSetSpeed(arg);
        return;
    }
    if (action == ACT_SLEEP) {
        appSetSleep(app, arg > 0 ? arg : 0);
        PlayerStatus s;
        playerGetStatus(&s);
        if (arg < 0 && appBookPlaying(app, &s)) {
            t->sleep_chapter = true;
            t->sleep_at = bookChapterIndex(&t->book, bookTimeOf(app, &s));
        }
        return;
    }
    if (action == ACT_SPEED_MENU || action == ACT_SLEEP_MENU) {
        if (action == ACT_SPEED_MENU)
            appBooksOpenSpeed(app);
        else
            appBooksOpenSleep(app);
        return;
    }
    if (action == ACT_RESCAN) {
        appBooksRescan(app);
        appToast(app, T("Looking for audiobooks..."));
        return;
    }
    Book b;
    if (!menuBook(app, &b))
        return;
    switch (action) {
    case ACT_CHAPTER:
        if ((size_t)arg < b.chapters.count)
            startAt(app, &b, b.chapters.items[arg].start);
        break;
    case ACT_PLAY:
        if (isPlaying(app, &b)) {
            appSetView(app, VIEW_NOW_PLAYING);
        } else {
            startBook(app, &b, -1.0, true);
            appSetView(app, VIEW_NOW_PLAYING);
        }
        break;
    case ACT_CHAPTERS:
        bookFree(&b);
        openChaptersMenu(app);
        return;
    case ACT_START_OVER: startAt(app, &b, 0.0); break;
    case ACT_FINISHED:
        if (isPlaying(app, &b)) {
            PlayerStatus s;
            playerGetStatus(&s);
            if (s.state == PLAYER_PLAYING)
                playerTogglePause();
            appBooksStop(app);
        }
        bookStoreSet(&t->store, b.key, 0, 0.0, nowSeconds(), true);
        t->order_stale = true;
        saveStore(app);
        break;
    case ACT_NOT_FINISHED:
        bookStoreForget(&t->store, b.key);
        t->order_stale = true;
        saveStore(app);
        break;
    default: break;
    }
    bookFree(&b);
}
