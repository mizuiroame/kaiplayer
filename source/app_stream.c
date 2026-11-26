// The Internet Archive (stream.h) as a source: the Library tab's pages for it (its first page is the
// Library's while it's the source), Home's while it is, and the console's network for them. Each page asks
// the Archive once, on a thread (streamnet.c), and keeps the answer while it's in the Library's stack of
// pages; a search's "Show 50 more" asks for the next ones. Nothing to set up: its Settings page has its
// color only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "app_buttons.h"
#include "kfile.h"
#include "keyboard_system.h"
#include "lang.h"
#include "net.h"
#include "streamfile.h"
#include "streammeta.h"
#include "streamnet.h"
#include "ui.h"

#define PAGE_ROWS UI_LIST_ROWS
#define ARCHIVE_NAME "Internet Archive"  // A name: not translated.

static bool g_network;
static unsigned g_last_serial;  // The newest request: older ones were called off.

bool appStreamEnsureNetwork(App *app)
{
    if (!g_network) {
        g_network = netInit();
        if (g_network) {
            kfileSetRemote(KFILE_REMOTE_STREAM, streamFileRemote());  // Its songs can be read from now on.
            streamNetStart();
        }
    }
    if (g_network && !app->config.online_used) {  // From now on the network starts with KaiPlayer.
        app->config.online_used = true;
        configSave(&app->config);
    }
    return g_network;
}

void appStreamStart(App *app)
{
    if (app->config.online_used || app->config.source == LIBRARY_ARCHIVE)
        appStreamEnsureNetwork(app);  // A playlist, the resumed queue or the source shown may need it.
}

// --- Pages ---

static StreamPage *topPage(App *app)
{
    return (StreamPage *)appStreamPage(app);
}

// Asks for the page's results (again).
static void startFetch(App *app, StreamPage *s)
{
    streamListFree(&s->list);
    s->query.page = 1;
    s->appending = false;
    s->failed = false;
    s->why[0] = '\0';
    s->serial = appStreamEnsureNetwork(app) ? streamFetch(&s->query) : 0;
    s->loading = s->serial != 0;
    if (s->serial) {
        g_last_serial = s->serial;
    } else {
        s->failed = true;
        snprintf(s->why, sizeof(s->why), "%s", T("The network can't be used."));
    }
}

// A new page on the Library's stack. False if there's no room (it's deep enough already).
static bool push(App *app, StreamPageKind kind, const StreamQuery *query, const char *title)
{
    LibraryTab *t = &app->library;
    appLibraryPage(app);  // At least the first page.
    if (t->depth >= LIBRARY_DEPTH_MAX)
        return false;
    StreamPage *s = &app->stream.pages[t->depth];
    streamListFree(&s->list);
    memset(s, 0, sizeof(*s));
    s->list.total = -1;
    s->kind = kind;
    if (query)
        s->query = *query;
    snprintf(s->title, sizeof(s->title), "%s", title);
    t->stack[t->depth++] = (LibraryPage){.level = LIB_STREAM};
    app->focus = FOCUS_LIST;
    if (kind == STREAM_PAGE_RESULTS)
        startFetch(app, s);
    return true;
}

// The next 50 of a search, to go after those shown.
static void fetchMore(App *app, StreamPage *s)
{
    if (s->appending || !appStreamEnsureNetwork(app))
        return;
    StreamQuery next = s->query;
    next.page = (s->query.page < 1 ? 1 : s->query.page) + 1;  // Not from the count: invalid items are left out.
    unsigned serial = streamFetch(&next);
    if (!serial)
        return;
    s->query.page = next.page;
    s->serial = g_last_serial = serial;
    s->appending = true;
}

static void pushQuery(App *app, StreamQueryKind kind, const char *words, const char *id, const char *title)
{
    StreamQuery q = {.kind = kind};
    snprintf(q.words, sizeof(q.words), "%s", words ? words : "");
    snprintf(q.id, sizeof(q.id), "%s", id ? id : "");
    push(app, STREAM_PAGE_RESULTS, &q, title);
}

// A search row: the words typed first (the last ones offered again).
static void search(App *app, int row)
{
    char typed[STREAM_WORDS_MAX], words[STREAM_WORDS_MAX], title[STREAM_TEXT_MAX];
    const char *label = streamMenuLabel(row);
    SystemKeyboardResult result = systemKeyboardText(label, app->stream.words, typed, sizeof(typed), sizeof(typed) - 1);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."));
        return;
    }
    if (result != SYSTEM_KEYBOARD_OK)
        return;
    if (!streamSearchWords(typed, words, sizeof(words))) {
        appToast(app, T("Type some words to search for."));
        return;
    }
    snprintf(app->stream.words, sizeof(app->stream.words), "%s", typed);
    snprintf(title, sizeof(title), "%s: %s", label, typed);
    pushQuery(app, streamMenuQuery(row), typed, NULL, title);
}

static void openMenuRow(App *app, size_t row)
{
    int r = (int)row;
    char title[STREAM_TEXT_MAX];
    snprintf(title, sizeof(title), ARCHIVE_NAME "  \xC2\xB7  %s", streamMenuLabel(r));
    if (streamMenuSearches(r))
        search(app, r);
    else if (streamMenuQuery(r) != STREAM_Q_COUNT)
        pushQuery(app, streamMenuQuery(r), NULL, NULL, title);
}

bool appStreamSongs(App *app, bool whole_page, TrackList *out, size_t *start)
{
    *out = (TrackList){0};
    *start = 0;
    const StreamPage *s = appStreamPage(app);
    if (!appStreamPageHasSongs(s))
        return false;
    size_t cursor = appLibraryPage(app)->cursor;
    for (size_t i = 0; i < s->list.count; i++) {
        const StreamItem *item = &s->list.items[i];
        if (item->kind != STREAM_ITEM_TRACK)
            continue;
        if (i == cursor)
            *start = out->count;
        if ((whole_page || i == cursor) && !trackListAdd(out, item->location)) {
            trackListFree(out);
            return false;
        }
    }
    if (!whole_page)
        *start = 0;
    return out->count > 0;
}

// Plays the page's songs from the selected one (or from the first), or shuffled. An item's songs are
// one album (a concert, a record).
static void play(App *app, bool from_cursor, bool shuffle)
{
    TrackList list;
    size_t start = 0;
    if (!appStreamSongs(app, true, &list, &start)) {
        appToast(app, T("There are no songs to play."));
        return;
    }
    appStreamEnsureNetwork(app);
    appPlayTracks(app, &list, from_cursor ? start : 0, shuffle, NULL);
    trackListFree(&list);
}

static void openResult(App *app, StreamPage *s, size_t row)
{
    if (s->failed && s->list.count == 0) {  // "Try again".
        startFetch(app, s);
        return;
    }
    if (row == s->list.count && appStreamHasMore(s)) {  // "Show 50 more".
        fetchMore(app, s);
        return;
    }
    if (row >= s->list.count)
        return;
    const StreamItem *item = &s->list.items[row];
    if (item->kind == STREAM_ITEM_TRACK)
        play(app, true, false);
    else
        pushQuery(app, STREAM_Q_ITEM, NULL, item->id, item->title);
}

static void openRow(App *app)
{
    StreamPage *s = topPage(app);
    size_t row = appLibraryPage(app)->cursor;
    if (!s || row >= appStreamCount(app))
        return;
    if (s->kind == STREAM_PAGE_MENU)
        openMenuRow(app, row);
    else
        openResult(app, s, row);
}

void appHandleStream(App *app, uint64_t pressed)
{
    LibraryPage *p = appLibraryPage(app);
    size_t count = appStreamCount(app);
    bool songs = appStreamPageHasSongs(topPage(app));
    bool first = app->library.depth <= 1;  // Its first page: the Library's while it's the source.
    pressed = appFocusFilter(app, pressed, p->cursor, count, !first, songs);

    long cursor = (long)p->cursor;
    if (pressed & BUTTONS_UP) cursor--;
    if (pressed & BUTTONS_DOWN) cursor++;
    if (pressed & BUTTONS_PAGE_UP) cursor -= PAGE_ROWS;
    if (pressed & BUTTONS_PAGE_DOWN) cursor += PAGE_ROWS;
    cursor = cursor >= (long)count ? (long)count - 1 : cursor;
    p->cursor = cursor < 0 ? 0 : (size_t)cursor;

    if ((pressed & BUTTON_ROW_ACTION) && songs && p->cursor < topPage(app)->list.count)
        appAddToPlaylist(app, SRC_LIBRARY_ENTRY);
    if (pressed & HidNpadButton_A) {
        if (app->focus == FOCUS_BACK)
            appLibraryBack(app);
        else
            openRow(app);
    }
    if (pressed & HidNpadButton_B)
        appLibraryBack(app);
    // X plays and Y shuffles an item's songs, as an album's in the library. On the first page, X goes to the
    // next source, as on the Library's first page of the others.
    if ((pressed & HidNpadButton_X) && first)
        appSwitchLibrarySource(app);
    else if ((pressed & HidNpadButton_X) && songs)
        play(app, false, false);
    if ((pressed & HidNpadButton_Y) && songs)
        play(app, false, true);
}

// Keeps the names of an answer's songs, for the lists and Now Playing.
static void keepNames(const StreamList *list)
{
    for (size_t k = 0; k < list->count; k++) {
        const StreamItem *item = &list->items[k];
        if (item->kind == STREAM_ITEM_TRACK)
            streamMetaPut(item->location, item->title, item->artist, item->album);
    }
}

// The next 50 that came: after those shown (taken from list). A failure leaves those shown, and says why.
static void appendAnswer(App *app, StreamPage *s, bool ok, StreamList *list, const char *why)
{
    s->appending = false;
    size_t room = s->list.count + list->count;
    StreamItem *items = ok && list->count ? realloc(s->list.items, room * sizeof(StreamItem)) : NULL;
    if (items) {
        memcpy(items + s->list.count, list->items, list->count * sizeof(StreamItem));
        keepNames(list);
        s->list.items = items;
        s->list.count = s->list.capacity = room;
        if (list->total >= 0)
            s->list.total = list->total;
    } else if (ok && list->count) {
        s->query.page--;  // Not shown: asked for again next time.
        appNotify(app, NOTICE_ERROR, T("Not enough memory."));
    } else if (ok) {
        s->list.total = (long)s->list.count;  // Nothing more: no "more" row.
    } else {
        s->query.page--;
        if (why[0])
            appNotify(app, NOTICE_ERROR, why);
    }
    streamListFree(list);
}

// An answer that came, put on its page (if that page is still there and still waiting for it).
static void takeAnswer(App *app)
{
    unsigned serial;
    bool ok;
    StreamList list;
    char why[STREAM_WHY_MAX];
    if (!streamFetchTake(&serial, &ok, &list, why, sizeof(why)))
        return;
    LibraryTab *t = &app->library;
    for (int i = 0; i < t->depth; i++) {
        StreamPage *s = &app->stream.pages[i];
        bool waiting = s->loading || s->appending;
        if (t->stack[i].level != LIB_STREAM || s->kind != STREAM_PAGE_RESULTS || !waiting || s->serial != serial)
            continue;
        if (s->appending) {
            appendAnswer(app, s, ok, &list, why);
            return;
        }
        keepNames(&list);
        streamListFree(&s->list);
        s->list = list;
        s->loading = false;
        s->failed = !ok;
        snprintf(s->why, sizeof(s->why), "%s", why);
        if (!ok && why[0])
            appNotify(app, NOTICE_ERROR, why);
        if (t->stack[i].cursor >= list.count)
            t->stack[i].cursor = 0;
        return;
    }
    streamListFree(&list);  // Its page is gone.
}

void appStreamUpdate(App *app)
{
    takeAnswer(app);
    StreamPage *s = topPage(app);
    if (s && s->kind == STREAM_PAGE_RESULTS && s->loading && s->serial != g_last_serial)
        startFetch(app, s);  // Its request was called off by a newer one (a page left before it came).
    else if (s && s->appending && s->serial != g_last_serial) {
        s->appending = false;  // Called off too: its "more" row can be chosen again.
        s->query.page--;
    }
}

bool appStreamMenuSong(App *app, char *location, size_t size)
{
    if (app->view == VIEW_NOW_PLAYING) {
        PlayerStatus status;
        playerGetStatus(&status);
        return status.state != PLAYER_STOPPED && playerTrackPath(status.track, location, size) &&
               streamIsLocation(location);
    }
    const StreamPage *s = app->view == VIEW_BROWSER ? appStreamPage(app) : NULL;
    size_t row = appLibraryPage(app)->cursor;
    if (!appStreamPageHasSongs(s) || row >= s->list.count || s->list.items[row].kind != STREAM_ITEM_TRACK)
        return false;
    snprintf(location, size, "%s", s->list.items[row].location);
    return true;
}

void appStreamHandleHome(App *app, uint64_t pressed)
{
    if (app->focus == FOCUS_PLAYER) {  // The player bar under Home.
        appFocusFilter(app, pressed, 0, 0, false, false);
        return;
    }
    pressed = appFocusFilter(app, pressed, 0, 1, false, false);
    if (pressed & HidNpadButton_X)
        appSwitchLibrarySource(app);
    else if (pressed & HidNpadButton_A)
        appSetView(app, VIEW_BROWSER);  // Its first page.
}

void appStreamExit(void)
{
    streamNetStop();
    kfileSetRemote(KFILE_REMOTE_STREAM, NULL);
    streamFileForget();
    streamMetaClear();
    if (g_network)
        netExit();
    g_network = false;
}
