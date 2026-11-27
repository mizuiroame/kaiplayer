// Renders the real UI code into PPM images on the host, with a fake player
// and artwork, so the design can be checked without a console.
// Usage: preview OUTDIR [COVER.jpg]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "app.h"
#include "artwork.h"
#include "bookscan.h"
#include "catalog_scan.h"
#include "covercache.h"
#include "font.h"
#include "lang.h"
#include "meta.h"
#include "gfx.h"
#include "player.h"
#include "theme.h"
#include "ui.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "ui_nowstyle.h"
#include "nowstyle.h"
#include "stream.h"
#include "streammeta.h"

static const char *const TRACKS[] = {
    "sdmc:/Music/Aurora Fields/01 - Morning Light.flac",
    "sdmc:/Music/Aurora Fields/02 - Paper Boats.flac",
    "sdmc:/Music/Aurora Fields/03 - Canción del Río (Versión Acústica Extendida).flac",
    "sdmc:/Music/Aurora Fields/04 - Neon Harbor.flac",
    "sdmc:/Music/Night Drive/01 - Overpass.mp3",
    "sdmc:/Music/Night Drive/02 - Ñandú Express.mp3",
    "sdmc:/Music/Night Drive/03 - Glass City.ogg",
    "sdmc:/Music/東京の夜/01 - 夜明けの歌.flac",
    "sdmc:/Music/東京の夜/02 - 桜の木の下で.flac",
};
#define TRACK_COUNT (sizeof(TRACKS) / sizeof(TRACKS[0]))

static Artwork g_art;
static CoverImage g_art_card, g_art_thumb_small;  // The sample cover at the Home sizes.
static bool g_has_art;

// Stubs for the modules that need the console.
static const char *g_playing_online;  // An Archive song playing instead of TRACKS[track] (renderStreamScenes).

bool playerTrackPath(size_t track, char *out, size_t size)
{
    if (g_playing_online && track == 2) {
        snprintf(out, size, "%s", g_playing_online);
        return true;
    }
    if (track >= TRACK_COUNT)
        return false;
    snprintf(out, size, "%s", TRACKS[track]);
    return true;
}

// A shuffled play order, so the Queue preview shows one.
static const size_t ORDER[] = {2, 5, 0, 7, 3, 8, 1, 6, 4};

bool playerQueueTrack(size_t pos, size_t *track)
{
    if (pos >= TRACK_COUNT)
        return false;
    *track = ORDER[pos];
    return true;
}

// Fake tags for the lists: songs in "Aurora Fields" have full tags (the
// title drops the track number), the others an artist or nothing.
static bool g_meta_loading;  // Tags not read yet: rows show placeholders.

bool metaLookup(const char *path, SongMeta *out)
{
    if (g_meta_loading)
        return false;
    if (streamMetaGet(path, out->title, sizeof(out->title), out->artist, sizeof(out->artist), out->album,
                      sizeof(out->album)))
        return true;  // An Archive song's, as its answer named it.
    bool aurora = strstr(path, "Aurora") != NULL;
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    const char *dash = strstr(name, " - ");
    snprintf(out->title, sizeof(out->title), "%.*s", aurora && dash ? (int)strcspn(dash + 3, ".") : 0,
             aurora && dash ? dash + 3 : "");
    snprintf(out->artist, sizeof(out->artist), "%s",
             aurora ? "Aurora Fields" : strstr(path, "Night") ? "The Night Drive Band" : "");
    snprintf(out->album, sizeof(out->album), "%s", aurora ? "Paper Boats & Other Stories" : "");
    return true;
}

// The library: a catalog built from the fake tracks' made-up tags.
static Catalog g_catalog;
static bool g_scan_busy;

const Catalog *catalogLock(void)
{
    return &g_catalog;
}

void catalogUnlock(void)
{
}

const char *playlistRoot(void)
{
    return "sdmc:/Music/Playlists";
}

void coverCacheLock(void)
{
}

void coverCacheUnlock(void)
{
}

const CoverImage *coverCacheGet(const char *song_path, bool thumb)
{
    // The sample cover stands in for every album that has one (and two of the books).
    bool has = strstr(song_path, "Aurora") || strstr(song_path, "Lighthouse") || strstr(song_path, "Paper Moon") ||
               strstr(song_path, "ia:/ndb2006") || strstr(song_path, "ia:/78_made");
    return g_art.has_cover && has ? (thumb ? &g_art_thumb_small : &g_art_card) : NULL;
}

// The audiobooks (bookscan.c, app_books.c): made-up books, set up by renderBookScenes.
static BookList g_book_list;
static bool g_book_busy;          // A look asked for or running.
static bool g_book_looked = true;  // A whole look has ended.
// As bookscan.c's lock, which doesn't let the same thread take it twice (that froze the tab on the
// console): taking it again here stops the preview instead of drawing on.
static bool g_book_locked;

const BookList *bookScanLock(void)
{
    if (g_book_locked) {
        fprintf(stderr, "bookScanLock taken twice: it would freeze the console\n");
        abort();
    }
    g_book_locked = true;
    return &g_book_list;
}

void bookScanUnlock(void)
{
    g_book_locked = false;
}

bool bookScanBusy(void)
{
    return booksStillLooking(true, g_book_looked, g_book_busy);
}

bool appBookPlaying(const App *app, const PlayerStatus *status)
{
    return app->books.active && status->book;
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

const BookMark *appBooksMark(const App *app, const char *key)
{
    return bookStoreFind(&app->books.store, key);
}

bool catalogScanProgress(size_t *done, size_t *total)
{
    *done = 312;
    *total = 480;
    return g_scan_busy;
}

// The network folder (app_network.c, kfile.c) and the server (app_server.c): set up in the scenes that show them.
static bool g_network, g_server;
static bool g_server_key;  // The server's sign-in is an API key.
static bool g_network_https;  // The network folder is a WebDAV one over https, its certificate check off.
static const char *g_scan_problem = "";
static bool g_net_scan;          // A library on the network being read,
static size_t g_net_scan_total;  // of these songs (0: its folders are still listed),
static bool g_net_scan_server;   // the server's.

bool catalogScanNetworkProgress(size_t *done, size_t *total, const char **root)
{
    *done = g_net_scan_total ? 1234 : 0;
    *total = g_net_scan_total;
    if (root)
        *root = g_net_scan_server ? "sub:/" : "dav:/";
    return g_net_scan;
}
#define PREVIEW_ADDRESS "smb://192.168.0.168:4451/Music"
#define PREVIEW_SERVER "192.168.0.168:4533"

void catalogScanProblem(const char *root, char *out, size_t size)
{
    (void)root;
    snprintf(out, size, "%s", g_scan_problem);
}

bool appNetworkReady(void)
{
    return g_network;
}

bool appServerReady(void)
{
    return g_server;
}

LibrarySource appLibrarySource(const App *app)
{
    return app->config.source;
}

bool appLibraryIsRemote(const App *app)
{
    return app->config.source == LIBRARY_NETWORK || app->config.source == LIBRARY_SERVER;
}

bool appLibraryIsArchive(const App *app)
{
    return app->config.source == LIBRARY_ARCHIVE;
}

bool appCanSwitchSource(const App *app)
{
    return g_network || g_server || app->config.archive_on || app->config.source != LIBRARY_CARD;
}

void appServerShown(char *address, size_t address_size, char *user, size_t user_size, bool *check_certificate)
{
    snprintf(address, address_size, "%s", g_server ? PREVIEW_SERVER : "");
    snprintf(user, user_size, "%s", g_server ? "kaiplayer" : "");
    *check_certificate = true;
}

bool appServerUsesKey(void)
{
    return g_server && g_server_key;
}

void appNetworkShown(char *address, size_t address_size, char *user, size_t user_size)
{
    snprintf(address, address_size, "%s", g_network_https ? "https://music.example.com/dav/" : g_network ? PREVIEW_ADDRESS : "");
    snprintf(user, user_size, "%s", g_network ? "kaiplayer" : "");
}

void appNetworkChoices(bool *remember, bool *allow_unsigned, bool *smb, bool *check_certificate)
{
    *remember = true;
    *allow_unsigned = false;
    *smb = g_network && !g_network_https;
    *check_certificate = !g_network_https;
}

// About's update rows: a newer version found, for the shot.
void appUpdatesValue(const App *app, int row, char *out, size_t size)
{
    (void)app;
    if (row == SETTING_UPDATE_AUTO)
        snprintf(out, size, "%s", T("On"));
    else
        snprintf(out, size, T("%s is out: A installs it"), "1.4.0");
}

void appNetworkShownPath(const char *location, char *out, size_t size)
{
    snprintf(out, size, "%s%s", PREVIEW_ADDRESS, strcmp(location, "dav:/") ? location + 4 : "");
}

static void buildCatalog(void)
{
    static const struct { const char *artist, *album; int year; } META[] = {
        {"Aurora Fields", "Paper Boats & Other Stories", 2024}, {"Aurora Fields", "Paper Boats & Other Stories", 2024},
        {"Aurora Fields", "Paper Boats & Other Stories", 2024}, {"Aurora Fields", "Neon Harbor (Single)", 2025},
        {"The Night Drive Band", "Night Drive", 2019}, {"The Night Drive Band", "Night Drive", 2019},
        {"The Night Drive Band", "Glass City", 2022}, {"八神純子", "東京の夜", 1983}, {"八神純子", "東京の夜", 1983},
    };
    catalogInit(&g_catalog);
    for (size_t i = 0; i < TRACK_COUNT; i++) {
        static TrackTags t;  // No picture, so nothing to free.
        memset(&t, 0, sizeof(t));
        const char *name = strrchr(TRACKS[i], '/') + 1, *dash = strstr(name, " - ");
        snprintf(t.title, sizeof(t.title), "%.*s", (int)strcspn(dash + 3, "."), dash + 3);
        snprintf(t.artist, sizeof(t.artist), "%s", META[i].artist);
        snprintf(t.album, sizeof(t.album), "%s", META[i].album);
        t.year = META[i].year;
        t.track_number = atoi(name);
        catalogAdd(&g_catalog, TRACKS[i], 1000, &t, "sdmc:/Music");
    }
    catalogBuild(&g_catalog);
}

// The parts of app_folder.c and app_browse.c that drawing uses.
const char *appMusicRoot(const App *app)
{
    if (app->config.source == LIBRARY_NETWORK)
        return "dav:/";
    if (app->config.source == LIBRARY_SERVER)
        return "sub:/";
    return app->config.library[0] ? app->config.library : "sdmc:/";
}

size_t appPickerFolderCount(const App *app)
{
    size_t n = 0;
    while (n < app->picker.count && app->picker.entries[n].is_dir)
        n++;
    return n;
}

bool appBrowseCanGoUp(const App *app)
{
    char parent[UTIL_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", app->browser.path);
    return utilParentPath(parent) && utilPathWithin(parent, appMusicRoot(app));
}

bool screenHasBacklightControl(void)
{
    return true;
}

const Artwork *artworkLock(void)
{
    return g_has_art ? &g_art : NULL;
}

void artworkUnlock(void)
{
}

static bool writePpm(const char *path, const uint32_t *pixels)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    fprintf(f, "P6\n%d %d\n255\n", GFX_FB_WIDTH, GFX_FB_HEIGHT);
    for (int i = 0; i < GFX_FB_WIDTH * GFX_FB_HEIGHT; i++) {
        uint32_t p = pixels[i];
        unsigned char rgb[3] = {p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
}

static void addEntry(Browser *b, const char *name, bool is_dir)
{
    b->entries = realloc(b->entries, (b->count + 1) * sizeof(BrowserEntry));
    b->entries[b->count++] = (BrowserEntry){strdup(name), is_dir};
}

int g_touch_problems;

// Every touch target the scene drew must answer a tap: with an overlay open,
// all those above its backdrop are overlay targets; with none, no target is
// one (app_touch.c ignores the other kind). Prints what wouldn't answer.
static void checkTouch(const char *name, const App *app)
{
    bool overlay = uiOverlayOpen(app);
    int backdrop = -1;
    for (int i = 0; i < uiHitCount(); i++)
        if (uiHitAt(i)->kind == HIT_BACKDROP)
            backdrop = i;
    if (overlay && backdrop < 0) {
        printf("TOUCH: %s: an overlay is open but drew no backdrop\n", name);
        g_touch_problems++;
    }
    for (int i = overlay ? backdrop + 1 : 0; i < uiHitCount(); i++) {
        const HitRegion *r = uiHitAt(i);
        if (r->kind != HIT_NOTICE && uiHitIsOverlayTarget(r->kind) != overlay) {  // The notice answers over any layer.
            printf("TOUCH: %s: target kind %d at %d,%d wouldn't answer a tap\n", name, r->kind, r->x, r->y);
            g_touch_problems++;
        }
    }
}

// A tap in the middle of the first target of this kind (and arg, unless any_arg) lands on it.
static const HitRegion *tapTarget(HitKind kind, int arg, bool any_arg)
{
    for (int i = 0; i < uiHitCount(); i++) {
        const HitRegion *r = uiHitAt(i);
        if (r->kind == kind && (any_arg || r->arg == arg))
            return uiHitFind(r->x + r->w / 2, r->y + r->h / 2);
    }
    return NULL;
}

static void touchProblem(const char *name, const char *what)
{
    printf("TOUCH: %s: %s\n", name, what);
    g_touch_problems++;
}

// A menu's items each answer a tap, the line between them in the way of none.
static void checkMenuTouch(const char *name, const Menu *m)
{
    for (int i = 0; i < m->count && i < 9; i++) {
        const HitRegion *r = tapTarget(HIT_MENU_ITEM, i, false);
        if (!r || r->kind != HIT_MENU_ITEM || r->arg != i)
            touchProblem(name, "a menu item doesn't answer a tap");
    }
}

// Settings: the main page's rows that open a page answer with that row
// (app_touch.c opens its page); a page's Back answers.
static void checkSettingsTouch(const char *name, const App *app)
{
    if (app->settings_page == SETTINGS_PAGE_MAIN) {
        for (int page = 0; page < SETTINGS_PAGE_COUNT; page++) {
            if (page == SETTINGS_PAGE_MAIN)
                continue;
            const HitRegion *r = tapTarget(HIT_SETTING, settingsPageRow((SettingsPage)page), false);
            if (!r || r->kind != HIT_SETTING || settingsRowPage((SettingRow)r->arg) != (SettingsPage)page)
                touchProblem(name, "a page's row doesn't open it");
        }
        if (tapTarget(HIT_OVERLAY_BACK, 0, true))
            touchProblem(name, "the main page has a Back button");
        return;
    }
    const HitRegion *back = tapTarget(HIT_OVERLAY_BACK, 0, true);
    if (!back || back->kind != HIT_OVERLAY_BACK)
        touchProblem(name, "Back doesn't answer a tap");
}

static void render(const char *dir, const char *name, const App *app, const PlayerStatus *status)
{
    static uint32_t pixels[GFX_FB_WIDTH * GFX_FB_HEIGHT];
    char path[512];
    gfxSetTarget(pixels, GFX_FB_WIDTH);
    uiDraw(app, status, 0);
    gfxSetTarget(NULL, 0);
    checkTouch(name, app);
    if (app->menu.open && !app->keyboard_open)
        checkMenuTouch(name, &app->menu);
    else if (app->settings_open && !app->about_open && !app->eq_open && !app->picker_open && !app->keyboard_open)
        checkSettingsTouch(name, app);
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    if (!writePpm(path, pixels))
        fprintf(stderr, "cannot write %s\n", path);
}

static void loadArtwork(const char *cover_path)
{
    snprintf(g_art.path, sizeof(g_art.path), "%s", TRACKS[2]);
    snprintf(g_art.title, sizeof(g_art.title), "%s", "Canción del Río (Versión Acústica Extendida)");
    snprintf(g_art.artist, sizeof(g_art.artist), "%s", "Aurora Fields");
    snprintf(g_art.album, sizeof(g_art.album), "%s", "Paper Boats & Other Stories");
    g_art.has_cover = cover_path && coverLoadFromFolder(cover_path, &g_art.cover, gfxToFb(ARTWORK_LARGE_SIZE),
                                                         &g_art.thumb, gfxToFb(ARTWORK_SMALL_SIZE));
    if (g_art.has_cover)
        coverLoadFromFolder(cover_path, &g_art_card, gfxToFb(COVERCACHE_CARD), &g_art_thumb_small,
                            gfxToFb(COVERCACHE_THUMB));
    g_has_art = true;
}

static void openMainMenu(App *app, bool playlist_actions)
{
    menuOpen(&app->menu, "KaiPlayer");
    app->menu.kind = MENU_KIND_MAIN;
    if (playlist_actions) {
        menuAdd(&app->menu, T("Move song"), 0, 0);
        menuAdd(&app->menu, T("Remove song from playlist"), 0, 0);
        menuAdd(&app->menu, "Rename \"Road Trip 2026\"", 0, 0);
        menuAdd(&app->menu, "Delete \"Road Trip 2026\"", 0, 0);
    }
    menuAddDivider(&app->menu);
    menuAdd(&app->menu, T("Update the library"), 0, 0);
    menuAdd(&app->menu, T("Turn screen off (music keeps playing)"), 0, 0);
    menuAdd(&app->menu, T("Settings"), 0, 0);
    menuAdd(&app->menu, T("Exit KaiPlayer"), 0, 0);
}

// A made-up book: parts of the same length, a chapter each (titles), or chapters of its own in one file.
static void addBook(const char *folder, const char *title, const char *author, size_t parts, double part_seconds,
                    const char *const *chapters, size_t chapter_count, bool probed)
{
    Book b = {0};
    char path[UTIL_PATH_MAX];
    b.key = strdup(folder);
    snprintf(b.title, sizeof(b.title), "%s", title);
    snprintf(b.author, sizeof(b.author), "%s", author);
    for (size_t i = 0; i < parts; i++) {
        if (parts == 1)
            snprintf(path, sizeof(path), "%s", folder);
        else
            snprintf(path, sizeof(path), "%s/%02zu.mp3", folder, i + 1);
        bookAddPart(&b, path, 1000, probed ? part_seconds : 0.0);
    }
    if (probed) {
        if (parts == 1)
            for (size_t i = 0; i < chapter_count; i++)
                bookChaptersAdd(&b.chapters, part_seconds * i / chapter_count, chapters[i]);
        bookFinishProbe(&b, parts > 1 ? chapters : NULL);
    }
    bookListTake(&g_book_list, &b);
}

// The Audiobooks tab, a book in Now Playing, its menus, and the player bar while it plays.
static void renderBookScenes(const char *dir, App *app, PlayerStatus *status)
{
    static const char *const LIGHTHOUSE[] = {
        "The Storm", "A Light Across the Water", "Letters from the Mainland", "The Keeper's Logbook",
        "Fog", "What the Tide Brought", "Winter Rations", "The Inspector", "Signals", "The Long Night",
        "Morning", "Afterword",
    };
    static const char *const MOON[] = {"Prologue", "The Paper Moon", "Ink and Rain", "A Kite in July",
                                       "The Clockmaker", "Lanterns", "The Last Page", "Epilogue"};
    static const char *const TEA[] = {"Leaves", "Ships", "Cups"};
    addBook("sdmc:/audiobooks/Ada Example/The Lighthouse Keeper's Daughter", "The Lighthouse Keeper's Daughter",
            "Ada Example", 12, 1520.0, LIGHTHOUSE, 12, true);
    addBook("sdmc:/audiobooks/Paper Moon Stories.m4b", "Paper Moon Stories", "Bo Placeholder", 1, 3 * 3600.0 + 1260,
            MOON, 8, true);
    addBook("sdmc:/audiobooks/A Short History of Tea", "A Short History of Tea", "Cy Sample", 3, 2700.0, TEA, 3, true);
    addBook("sdmc:/audiobooks/Winter Garden", "Winter Garden", "Di Demo", 4, 1800.0, TEA, 0, true);
    addBook("dav:/Audiobooks/Night Train to Nowhere", "Night Train to Nowhere", "", 9, 0.0, NULL, 0, false);
    g_book_list.generation = 1;
    BookStore *store = &app->books.store;
    bookStoreSet(store, g_book_list.items[0].key, 4, 610.0, 3000, false);
    bookStoreSet(store, g_book_list.items[1].key, 0, 8100.0, 2000, false);
    bookStoreSet(store, g_book_list.items[3].key, 0, 0.0, 1000, true);
    static size_t order[] = {0, 1, 2, 4, 3};
    app->books.order = order;
    app->books.order_count = 5;
    app->books.order_generation = 1;
    app->books.cursor = 1;

    app->view = VIEW_BOOKS;
    app->focus = FOCUS_LIST;
    app->books.active = true;
    bookCopy(&app->books.book, &g_book_list.items[0]);
    *status = (PlayerStatus){
        .state = PLAYER_PLAYING, .track = 4, .queue_pos = 4, .track_count = 12, .position = 610.0,
        .duration = 1520.0, .book = true, .speed = 1250, .format = {"MP3", 44100, 0},
    };
    g_has_art = true;
    render(dir, "books", app, status);
    g_book_busy = true;
    render(dir, "books_looking", app, status);
    g_book_busy = false;
    app->focus = FOCUS_PLAYER;
    app->bar_control = BAR_SHUFFLE;
    render(dir, "books_player_bar", app, status);
    app->focus = FOCUS_LIST;

    app->view = VIEW_NOW_PLAYING;
    render(dir, "now_playing_book", app, status);
    status->state = PLAYER_PAUSED;
    status->speed = 1000;
    app->books.sleep_chapter = true;
    render(dir, "now_playing_book_paused", app, status);
    app->books.sleep_chapter = false;
    status->state = PLAYER_PLAYING;
    status->speed = 1250;

    // Its chapters, as appBooksOpenChapters lists them: the one playing checked, the times beside them.
    menuOpen(&app->menu, app->books.book.title);
    for (size_t i = 0; i < app->books.book.chapters.count; i++) {
        char label[MENU_LABEL_MAX], at[16];
        snprintf(label, sizeof(label), "%zu.  %s", i + 1, app->books.book.chapters.items[i].title);
        utilFormatTime(at, sizeof(at), app->books.book.chapters.items[i].start);
        menuAdd(&app->menu, label, 0, 0);
        menuMarkLast(&app->menu, at, i == 4);
    }
    app->menu.cursor = 4;
    render(dir, "book_chapters", app, status);
    menuOpen(&app->menu, T("Reading speed"));  // As appBooksOpenSpeed.
    static const char *const SPEEDS[] = {"0.75x", "1x", "1.25x", "1.5x", "1.75x", "2x"};
    for (int i = 0; i < 6; i++) {
        menuAdd(&app->menu, SPEEDS[i], 0, 0);
        menuMarkLast(&app->menu, i == 1 ? T("Normal") : "", i == 2);
    }
    app->menu.cursor = 2;
    render(dir, "book_speed", app, status);
    menuOpen(&app->menu, T("Sleep timer"));  // As appBooksOpenSleep.
    menuAdd(&app->menu, T("Off"), 0, 0);
    static const int MINUTES[] = {15, 30, 45, 60, 90};
    for (int i = 0; i < 5; i++) {
        char label[48];
        snprintf(label, sizeof(label), T("%d minutes"), MINUTES[i]);
        menuAdd(&app->menu, label, 0, 0);
    }
    menuAdd(&app->menu, T("End of chapter"), 0, 0);
    menuMarkLast(&app->menu, "", true);
    app->menu.cursor = 6;
    render(dir, "book_sleep", app, status);
    menuClose(&app->menu);

    // Empty, while it looks and after.
    app->books.active = false;
    status->book = false;
    app->view = VIEW_BOOKS;
    app->books.order_count = 0;
    g_book_looked = false;  // Started, no look yet (no books.cache): looking, not where to put books.
    render(dir, "books_empty_not_looked", app, status);
    g_book_busy = true;
    render(dir, "books_empty_looking", app, status);
    g_book_looked = true;  // Looking again after one that found none.
    render(dir, "books_empty_looking_again", app, status);
    g_book_busy = false;
    render(dir, "books_empty", app, status);
    app->books.order = NULL;
    bookFree(&app->books.book);
    bookStoreFree(store);
    bookListFree(&g_book_list);
}

static char *const PLAYLIST_NAMES[] = {"Chill evenings", "Road Trip 2026", "Workout"};

static void renderPlaylistScenes(const char *dir, App *app, PlayerStatus *status)
{
    app->view = VIEW_PLAYLISTS;
    app->playlists.index = (PlaylistIndex){(char **)PLAYLIST_NAMES, 3};
    app->playlists.cursor = 1;
    render(dir, "playlists", app, status);

    static char *paths[TRACK_COUNT];
    for (size_t i = 0; i < TRACK_COUNT; i++)
        paths[i] = (char *)TRACKS[i];
    app->playlists.open = true;
    snprintf(app->playlists.name, sizeof(app->playlists.name), "%s", "Road Trip 2026");
    app->playlists.tracks = (TrackList){paths, TRACK_COUNT, TRACK_COUNT};
    app->playlists.track_cursor = 3;
    app->playlists.moving = true;
    render(dir, "playlist_songs", app, status);
    app->playlists.moving = false;
    app->focus = FOCUS_BACK;
    render(dir, "playlist_focus_back", app, status);
    app->focus = FOCUS_LIST;

    // The + menu as app_menu.c builds it: this screen's actions, a line, the app's.
    openMainMenu(app, true);
    app->menu.cursor = 1;
    render(dir, "menu", app, status);
    openMainMenu(app, false);  // A screen with no actions of its own: no line.
    app->view = VIEW_NOW_PLAYING;
    render(dir, "menu_app_only", app, status);
    app->view = VIEW_PLAYLISTS;

    app->view = VIEW_BROWSER;
    menuOpen(&app->menu, "Add \"01 - Overpass.mp3\" to");
    menuAdd(&app->menu, "New playlist...", 0, 0);
    for (int i = 0; i < 3; i++)
        menuAdd(&app->menu, PLAYLIST_NAMES[i], 0, 0);
    app->menu.cursor = 2;
    render(dir, "picker", app, status);

    app->view = VIEW_NOW_PLAYING;
    menuOpen(&app->menu, "Exit KaiPlayer?");
    app->menu.kind = MENU_KIND_EXIT;
    menuAdd(&app->menu, "Exit", 0, 0);
    menuAdd(&app->menu, "Cancel", 0, 0);
    render(dir, "exit", app, status);
    menuClose(&app->menu);
    app->view = VIEW_BROWSER;

    app->keyboard_open = true;
    keyboardInit(&app->keyboard, "Late night dr");
    app->keyboard.row = 2;
    app->keyboard.col = 4;
    snprintf(app->keyboard_title, sizeof(app->keyboard_title), "%s", "Name for the new playlist");
    render(dir, "keyboard", app, status);
    app->keyboard_open = false;

    // Settings with the sleep timer running (24 minutes left at time 0).
    app->config.resume = RESUME_PLAYING;
    app->config.clock_24h = false;
    app->sleep_minutes = 30;
    app->sleep_deadline_ns = 24ull * 60000000000ull;
    app->view = VIEW_NOW_PLAYING;
    app->settings_open = true;
    app->settings_page = SETTINGS_PAGE_MAIN;
    app->settings_cursor = SETTING_OPEN_LIBRARY;
    render(dir, "settings", app, status);
    app->settings_page = SETTINGS_PAGE_LIBRARY;
    app->settings_cursor = SETTING_RESCAN;
    render(dir, "settings_library", app, status);
    app->settings_page = SETTINGS_PAGE_DISPLAY;
    app->settings_cursor = SETTING_NOW_STYLE;
    render(dir, "settings_display", app, status);
    app->settings_page = SETTINGS_PAGE_PLAYBACK;
    app->settings_cursor = SETTING_SLEEP;
    render(dir, "settings_playback", app, status);
    app->settings_page = SETTINGS_PAGE_NETWORK;  // The network folder's page, before and after it's set.
    app->settings_cursor = SETTING_NET_ADDRESS;
    render(dir, "settings_network_empty", app, status);
    g_network = true;
    app->settings_cursor = SETTING_NET_TEST;
    render(dir, "settings_network", app, status);
    g_network_https = true;
    app->settings_cursor = SETTING_NET_CERTIFICATE;
    render(dir, "settings_network_https", app, status);
    g_network_https = false;
    app->settings_page = SETTINGS_PAGE_MAIN;
    app->settings_cursor = SETTING_OPEN_NETWORK;
    render(dir, "settings_main_network", app, status);
    g_network = false;
    app->settings_page = SETTINGS_PAGE_SERVER;  // The server's page, before and after it's set.
    app->settings_cursor = SETTING_SRV_ADDRESS;
    render(dir, "settings_server_empty", app, status);
    g_server = true;
    app->settings_cursor = SETTING_SRV_CERTIFICATE;
    render(dir, "settings_server", app, status);
    g_server_key = true;  // Signed in with an API key instead.
    app->settings_cursor = SETTING_SRV_API_KEY;
    render(dir, "settings_server_key", app, status);
    g_server_key = false;
    app->settings_page = SETTINGS_PAGE_MAIN;
    app->settings_cursor = SETTING_OPEN_SERVER;
    render(dir, "settings_main_server", app, status);
    g_server = false;
    app->settings_page = SETTINGS_PAGE_ABOUT;  // About: the credits, and updates.
    app->settings_cursor = SETTING_UPDATE_NOW;
    render(dir, "settings_about", app, status);
    app->about_open = true;
    render(dir, "about", app, status);
    app->about_open = false;
    // A notice over Settings: the bubble at the top, over everything.
    noticePush(&app->notices, NOTICE_DONE, T("Saved: the card keeps the password's hash, which opens this share like the password. Use a user that can only read the music."), 1);
    app->notice_count = noticeVisible(&app->notices, 1 + NOTICE_SLIDE_NS, app->notice_views);
    render(dir, "notice_settings", app, status);
    app->notices = (Notices){0};
    app->notice_count = 0;
    app->settings_page = SETTINGS_PAGE_MAIN;
    app->eq_open = true;
    app->config.eq_on = true;
    app->config.eq_preset = EQ_PRESET_ROCK;
    app->eq_band = 5;
    render(dir, "equalizer", app, status);
    app->eq_open = false;
    app->settings_open = false;
    app->config.accent = THEME_ACCENT_FROM_COVER;
    app->view = VIEW_NOW_PLAYING;
    g_has_art = true;
    status->track = 2;  // The track with the sample cover.
    render(dir, "accent_from_cover", app, status);
    app->config.accent = 0;
}

// KAI_LANG_FILE names a language file: the screens are drawn in it (the
// Makefile draws every language in data/, to see what no longer fits).
static void useLanguageFile(void)
{
    const char *path = getenv("KAI_LANG_FILE");
    if (!path)
        return;
    FILE *f = fopen(path, "rb");
    static char text[1 << 20];
    size_t size = f ? fread(text, 1, sizeof(text), f) : 0;
    if (f)
        fclose(f);
    if (!size || !langLoadText(text, size))
        fprintf(stderr, "cannot read the language in %s\n", path);
}

// A made-up Archive song or item, kept as the pages' answers keep them.
static void addOnline(StreamList *list, StreamItemKind kind, const char *location, const char *title,
                      const char *artist, const char *album, const char *detail, const char *id)
{
    list->items = realloc(list->items, (list->count + 1) * sizeof(StreamItem));
    StreamItem *item = &list->items[list->count++];
    *item = (StreamItem){.kind = kind, .position = (int)list->count};
    snprintf(item->location, sizeof(item->location), "%s", location);
    snprintf(item->title, sizeof(item->title), "%s", title);
    snprintf(item->artist, sizeof(item->artist), "%s", artist);
    snprintf(item->album, sizeof(item->album), "%s", album);
    snprintf(item->detail, sizeof(item->detail), "%s", detail);
    snprintf(item->id, sizeof(item->id), "%s", id);
    if (kind == STREAM_ITEM_TRACK)
        streamMetaPut(location, title, artist, album);
}

// An Archive page at depth (1-based) of the Library's stack.
static StreamPage *onlinePage(App *app, int depth, StreamPageKind kind, StreamQueryKind query, const char *title)
{
    StreamPage *s = &app->stream.pages[depth - 1];
    streamListFree(&s->list);
    *s = (StreamPage){.kind = kind, .query = {.kind = query}};
    snprintf(s->title, sizeof(s->title), "%s", title);
    app->library.stack[depth - 1] = (LibraryPage){.level = LIB_STREAM};
    app->library.depth = depth;
    return s;
}

#define LIVE_ITEM "ndb2006-01-14.made.up"
#define LIVE_TITLE "The Night Drive Band Live at a Made-Up Hall"

// The Internet Archive as the source: Home, its first page with the pill, its lists (with "Show 50 more"),
// an item's songs, the + menu, Now Playing's credits, and its Settings page.
static void renderStreamScenes(const char *dir, App *app, PlayerStatus *status)
{
    app->config.source = LIBRARY_ARCHIVE;
    app->focus = FOCUS_LIST;
    app->view = VIEW_HOME;
    render(dir, "home_archive_source", app, status);
    app->view = VIEW_BROWSER;
    app->library.depth = 0;  // Its first page, as Library's.
    appLibraryPage(app);
    render(dir, "online_archive", app, status);
    app->library.stack[0].cursor = 2;
    render(dir, "online_archive_78_row", app, status);
    app->library.stack[0].cursor = 0;

    char title[STREAM_TEXT_MAX];
    snprintf(title, sizeof(title), "Internet Archive  \xC2\xB7  %s", streamMenuLabel(0));
    StreamPage *s = onlinePage(app, 2, STREAM_PAGE_RESULTS, STREAM_Q_POPULAR_LIVE, title);
    s->loading = true;
    render(dir, "online_loading", app, status);
    s->loading = false;
    s->failed = true;
    snprintf(s->why, sizeof(s->why), "%s", T("The Internet Archive didn't answer. Is the console connected to the internet?"));
    render(dir, "online_failed", app, status);
    s->failed = false;
    addOnline(&s->list, STREAM_ITEM_ARCHIVE, "ia:/" LIVE_ITEM "/cover/0.jpg", LIVE_TITLE, "The Night Drive Band", "",
              "2006-01-14", LIVE_ITEM);
    addOnline(&s->list, STREAM_ITEM_ARCHIVE, "ia:/af1999-07-01.sbd/cover/0.jpg", "Aurora Fields at the Pier",
              "Aurora Fields", "", "1999-07-01", "af1999-07-01.sbd");
    s->list.total = 307208;  // More than shown: a "Show 50 more" row.
    render(dir, "online_archive_concerts", app, status);
    app->library.stack[1].cursor = 2;
    render(dir, "online_archive_more", app, status);
    s->appending = true;
    render(dir, "online_archive_more_asking", app, status);
    s->appending = false;
    app->library.stack[1].cursor = 0;
    snprintf(title, sizeof(title), "Internet Archive  \xC2\xB7  %s", streamMenuLabel(2));
    s = onlinePage(app, 2, STREAM_PAGE_RESULTS, STREAM_Q_POPULAR_78, title);
    addOnline(&s->list, STREAM_ITEM_ARCHIVE, "ia:/78_made-up-waltz/cover/0.jpg", "Made-Up Waltz",
              "The Paper Moon Orchestra", "", "1928", "78_made-up-waltz");
    addOnline(&s->list, STREAM_ITEM_ARCHIVE, "ia:/78_lighthouse-blues/cover/0.jpg", "Lighthouse Blues",
              "Neon Harbor Quartet", "", "1931", "78_lighthouse-blues");
    render(dir, "online_archive_78s", app, status);

    size_t was_track = status->track;
    status->track = 2;  // g_playing_online stands in for it.
    s = onlinePage(app, 3, STREAM_PAGE_RESULTS, STREAM_Q_ITEM, LIVE_TITLE);
    addOnline(&s->list, STREAM_ITEM_TRACK, "ia:/" LIVE_ITEM "/lma/ndb2006-01-14d1t01.mp3", "Morning Light",
              "The Night Drive Band", LIVE_TITLE, "", "");
    addOnline(&s->list, STREAM_ITEM_TRACK, "ia:/" LIVE_ITEM "/lma/ndb2006-01-14d1t02.mp3", "Second Song",
              "The Night Drive Band", LIVE_TITLE, "", "");
    addOnline(&s->list, STREAM_ITEM_TRACK, "ia:/" LIVE_ITEM "/lma/ndb2006-01-14d1t03.mp3", "Encore",
              "The Night Drive Band", LIVE_TITLE, "", "");
    app->library.stack[2].cursor = 1;
    g_playing_online = s->list.items[0].location;
    render(dir, "online_archive_concert", app, status);

    menuOpen(&app->menu, "KaiPlayer");  // The + menu under the Archive: its song's credits, the next source.
    app->menu.kind = MENU_KIND_MAIN;
    menuAdd(&app->menu, T("About this song"), 0, 0);
    menuAddDivider(&app->menu);
    menuAdd(&app->menu, T("Go to the SD card library"), 0, 0);
    menuAdd(&app->menu, T("Settings"), 0, 0);
    menuAdd(&app->menu, T("Exit KaiPlayer"), 0, 0);
    render(dir, "menu_archive_source", app, status);
    menuClose(&app->menu);

    menuOpen(&app->menu, "Morning Light");  // "About this song".
    char line[MENU_LABEL_MAX], text[160];
    snprintf(line, sizeof(line), T("By %s"), "The Night Drive Band");
    menuAdd(&app->menu, line, 0, 0);
    streamLicenseName("lma", text, sizeof(text));
    snprintf(line, sizeof(line), T("License: %s"), text);
    menuAdd(&app->menu, line, 0, 0);
    menuAdd(&app->menu, "archive.org/details/etree", 0, 0);
    snprintf(line, sizeof(line), T("From %s: %s"), "Internet Archive", "archive.org/details/" LIVE_ITEM);
    menuAdd(&app->menu, line, 0, 0);
    menuAdd(&app->menu, T("Close"), 0, 0);
    app->view = VIEW_NOW_PLAYING;
    Artwork card_art = g_art;  // The Archive song's names over the sample cover.
    bool had_art = g_has_art;
    g_has_art = true;
    snprintf(g_art.path, sizeof(g_art.path), "%s", g_playing_online);
    snprintf(g_art.title, sizeof(g_art.title), "Morning Light");
    snprintf(g_art.artist, sizeof(g_art.artist), "The Night Drive Band");
    snprintf(g_art.album, sizeof(g_art.album), "%s", LIVE_TITLE);
    AudioFormat card_format = status->format;
    status->format = (AudioFormat){"MP3", 44100, 0, 0};
    render(dir, "online_about_song", app, status);
    menuClose(&app->menu);
    render(dir, "now_playing_online", app, status);
    g_playing_online = "ia:/madeup-netlabel-001/by-sa-4.0/01%20-%20Quiet%20Room.ogg";
    snprintf(g_art.path, sizeof(g_art.path), "%s", g_playing_online);
    snprintf(g_art.title, sizeof(g_art.title), "Quiet Room");
    snprintf(g_art.artist, sizeof(g_art.artist), "Paper Moons");
    snprintf(g_art.album, sizeof(g_art.album), "Quiet Rooms EP");
    status->format = (AudioFormat){"OGG", 44100, 0, 0};
    render(dir, "now_playing_online_cc", app, status);
    g_playing_online = NULL;
    g_art = card_art;
    g_has_art = had_art;
    status->format = card_format;
    status->track = was_track;

    app->settings_open = true;  // Its Settings page: its color only.
    app->settings_page = SETTINGS_PAGE_ARCHIVE;
    app->settings_cursor = SETTING_IA_ENABLED;
    render(dir, "settings_archive", app, status);
    app->config.archive_on = false;  // Turned off: out of the sources (the card's shows), Off on the main page.
    app->config.source = LIBRARY_CARD;
    render(dir, "settings_archive_off", app, status);
    app->settings_page = SETTINGS_PAGE_MAIN;
    app->settings_cursor = SETTING_OPEN_ARCHIVE;
    render(dir, "settings_main_archive_off", app, status);
    app->settings_open = false;
    app->view = VIEW_HOME;
    render(dir, "home_archive_off", app, status);
    app->view = VIEW_NOW_PLAYING;
    app->settings_open = true;
    app->config.archive_on = true;
    app->config.source = LIBRARY_ARCHIVE;
    app->settings_page = SETTINGS_PAGE_ARCHIVE;
    app->settings_page = SETTINGS_PAGE_MAIN;
    app->settings_cursor = SETTING_OPEN_ARCHIVE;
    render(dir, "settings_main_archive", app, status);
    app->settings_open = false;
    app->config.source = LIBRARY_CARD;
    app->view = VIEW_BROWSER;
    app->library.depth = 1;
    app->library.stack[0] = (LibraryPage){.level = LIB_ROOT};
}

int main(int argc, char **argv)
{
    useLanguageFile();
    if (argc < 2 || !fontInit()) {
        fprintf(stderr, "usage: preview OUTDIR [TRACK_IN_COVER_FOLDER]\n");
        return 2;
    }
    static App app;
    app.config.archive_on = true;  // As configLoad leaves it.
    app.audio_ok = true;
    app.sys = (SysInfo){.has_time = true, .hour = 21, .minute = 47,
                        .has_battery = true, .battery_percent = 72, .charging = false};
    snprintf(app.browser.path, sizeof(app.browser.path), "%s", "sdmc:/Music");
    addEntry(&app.browser, "Aurora Fields", true);
    addEntry(&app.browser, "Night Drive", true);
    addEntry(&app.browser, "Soundtracks", true);
    addEntry(&app.browser, "東京の夜", true);
    addEntry(&app.browser, "intro.mp3", false);
    addEntry(&app.browser, "voice memo 2026-11-02.wav", false);
    app.browser.cursor = 1;

    PlayerStatus status = {
        .state = PLAYER_PLAYING, .track = 2, .queue_pos = 0, .track_count = TRACK_COUNT,
        .position = 83.0, .duration = 247.0, .shuffle = true, .repeat = REPEAT_ALL,
        .format = {"FLAC", 96000, 24},
    };
    loadArtwork(argc > 2 ? argv[2] : NULL);

    app.view = VIEW_BROWSER;
    snprintf(app.config.library, sizeof(app.config.library), "%s", "sdmc:/Music");
    app.library.depth = 1;
    app.library.stack[0] = (LibraryPage){.level = LIB_FOLDERS};  // Library > Folders.
    render(argv[1], "browse", &app, &status);

    // The Library tab's pages.
    buildCatalog();

    // Home, with some plays and playlists in the history.
    historyInit(&app.history);
    static const int PLAYS[] = {0, 1, 4, 5, 0, 7, 2, 0};
    for (size_t i = 0; i < sizeof(PLAYS) / sizeof(PLAYS[0]); i++)
        historyAdd(&app.history, HISTORY_SONG, TRACKS[PLAYS[i]]);
    historyAdd(&app.history, HISTORY_PLAYLIST, "Road Trip 2026");
    historyAdd(&app.history, HISTORY_PLAYLIST, "Chill evenings");
    app.view = VIEW_HOME;
    app.home_row = HOME_RECENT_ALBUMS;
    app.home_col[HOME_RECENT_ALBUMS] = 1;
    render(argv[1], "home", &app, &status);
    app.home_row = HOME_RECENT_ARTISTS;
    render(argv[1], "home_scrolled", &app, &status);
    app.view = VIEW_BROWSER;
    app.library.depth = 1;
    app.library.stack[0] = (LibraryPage){.level = LIB_ROOT};
    g_scan_busy = true;
    render(argv[1], "library", &app, &status);
    g_scan_busy = false;
    app.library.stack[1] = (LibraryPage){.level = LIB_ARTISTS, .cursor = 1};
    app.library.depth = 2;
    render(argv[1], "library_artists", &app, &status);
    app.library.stack[2] = (LibraryPage){.level = LIB_ARTIST};
    app.library.depth = 3;
    render(argv[1], "library_artist", &app, &status);
    app.library.stack[3] = (LibraryPage){.level = LIB_ALBUM, .arg = g_catalog.artists[0].albums[1]};
    app.library.depth = 4;
    render(argv[1], "library_album", &app, &status);
    // The network folder as the library: its source in Library, its Folders, and what went wrong.
    g_network = true;
    app.view = VIEW_HOME;  // The source in the header, and the network folder read in the background.
    render(argv[1], "home_network_source", &app, &status);
    g_net_scan = true;
    g_net_scan_total = 1602;
    render(argv[1], "home_network_scan", &app, &status);
    g_net_scan_total = 0;
    app.view = VIEW_NOW_PLAYING;
    render(argv[1], "now_playing_network_listing", &app, &status);
    app.sleep_deadline_ns = 24ull * 60000000000ull;  // Everything in the header at once.
    g_net_scan_total = 1602;
    render(argv[1], "now_playing_header_full", &app, &status);
    app.sleep_deadline_ns = 0;
    g_net_scan = false;
    menuOpen(&app.menu, "KaiPlayer");  // The + menu's way to the other library.
    app.menu.kind = MENU_KIND_MAIN;
    menuAddDivider(&app.menu);
    menuAdd(&app.menu, T("Update the library"), 0, 0);
    menuAdd(&app.menu, T("Go to the network folder library"), 0, 0);
    menuAdd(&app.menu, T("Turn screen off (music keeps playing)"), 0, 0);
    menuAdd(&app.menu, T("Settings"), 0, 0);
    menuAdd(&app.menu, T("Exit KaiPlayer"), 0, 0);
    app.menu.cursor = 1;
    render(argv[1], "menu_network_source", &app, &status);
    menuClose(&app.menu);
    app.view = VIEW_BROWSER;
    app.library.depth = 1;
    app.library.stack[0] = (LibraryPage){.level = LIB_ROOT, .cursor = 3};
    render(argv[1], "library_source_card", &app, &status);
    app.config.source = LIBRARY_NETWORK;
    render(argv[1], "library_network", &app, &status);
    g_scan_problem = "Nothing answered at that address. Is the computer on, and is the folder shared?";
    render(argv[1], "library_network_problem", &app, &status);
    g_scan_problem = "";
    static Browser card;
    card = app.browser;
    app.browser = (Browser){0};
    snprintf(app.browser.path, sizeof(app.browser.path), "%s", "dav:/Aurora Fields");
    addEntry(&app.browser, "Neon Harbor", true);
    addEntry(&app.browser, "Paper Boats", true);
    addEntry(&app.browser, "01 - Morning Light.flac", false);
    app.library.stack[0].level = LIB_FOLDERS;
    render(argv[1], "browse_network", &app, &status);
    browserFree(&app.browser);
    snprintf(app.browser.path, sizeof(app.browser.path), "%s", "dav:/");
    snprintf(app.browser.problem, sizeof(app.browser.problem), "%s", "Wrong user name or password.");
    render(argv[1], "browse_network_problem", &app, &status);
    app.browser = card;
    app.config.source = LIBRARY_CARD;

    // The server as the library: the pill, its scan, Library, its Folders, and what went wrong.
    g_server = true;
    app.config.source = LIBRARY_SERVER;
    app.view = VIEW_HOME;
    render(argv[1], "home_server_source", &app, &status);
    g_net_scan = g_net_scan_server = true;
    g_net_scan_total = 1602;
    render(argv[1], "home_server_scan", &app, &status);
    g_net_scan_total = 0;
    render(argv[1], "home_server_scan_counting", &app, &status);
    g_net_scan = g_net_scan_server = false;
    app.view = VIEW_BROWSER;
    app.library.depth = 1;
    app.library.stack[0] = (LibraryPage){.level = LIB_ROOT, .cursor = 1};
    render(argv[1], "library_server", &app, &status);
    g_scan_problem = "Wrong user name or password.";
    render(argv[1], "library_server_problem", &app, &status);
    g_scan_problem = T("No Navidrome or Subsonic server there. If it's under a path (like /navidrome), add it to the address.");
    render(argv[1], "library_server_path", &app, &status);
    g_scan_problem = "";
    card = app.browser;
    app.browser = (Browser){0};
    snprintf(app.browser.path, sizeof(app.browser.path), "%s", "sub:/");
    snprintf(app.browser.problem, sizeof(app.browser.problem), "%s",
             T("Navidrome's songs are in Artists, Albums and Songs: it has no folders here."));
    app.library.stack[0].level = LIB_FOLDERS;
    render(argv[1], "browse_server", &app, &status);
    app.browser = card;
    menuOpen(&app.menu, "KaiPlayer");  // With the network folder too, X goes on to the SD card.
    app.menu.kind = MENU_KIND_MAIN;
    menuAddDivider(&app.menu);
    menuAdd(&app.menu, T("Update the library"), 0, 0);
    menuAdd(&app.menu, T("Go to the SD card library"), 0, 0);
    menuAdd(&app.menu, T("Settings"), 0, 0);
    menuAdd(&app.menu, T("Exit KaiPlayer"), 0, 0);
    app.menu.cursor = 1;
    app.view = VIEW_HOME;
    render(argv[1], "menu_server_source", &app, &status);
    menuClose(&app.menu);
    app.view = VIEW_BROWSER;
    app.config.source = LIBRARY_CARD;
    g_server = false;
    g_network = false;

    app.library.stack[0].level = LIB_FOLDERS;  // The rest of the scenes show Folders.
    app.library.depth = 1;

    // The first run's welcome, then its SD card row: choosing the music folder.
    app.welcome_open = true;
    app.welcome_cursor = WELCOME_CARD;
    render(argv[1], "welcome", &app, &status);
    app.welcome_cursor = WELCOME_SERVER;
    render(argv[1], "welcome_server", &app, &status);
    app.welcome_open = false;
    snprintf(app.picker.path, sizeof(app.picker.path), "%s", "sdmc:/");
    addEntry(&app.picker, "Music", true);
    addEntry(&app.picker, "Podcasts", true);
    addEntry(&app.picker, "Nintendo", true);
    addEntry(&app.picker, "switch", true);
    addEntry(&app.picker, "notes.txt", false);
    app.picker_open = true;
    render(argv[1], "folder_picker", &app, &status);
    app.picker_open = false;
    app.focus = FOCUS_ADD;
    render(argv[1], "browse_focus_add", &app, &status);
    app.focus = FOCUS_LIST;
    app.view = VIEW_QUEUE;
    app.queue_cursor = 3;
    render(argv[1], "queue", &app, &status);
    g_meta_loading = true;
    render(argv[1], "queue_loading", &app, &status);
    g_meta_loading = false;
    app.focus = FOCUS_PLAYER;
    app.bar_control = BAR_NEXT;
    render(argv[1], "player_bar_focus", &app, &status);
    app.focus = FOCUS_LIST;
    app.view = VIEW_NOW_PLAYING;
    snprintf(app.playing_playlist, sizeof(app.playing_playlist), "%s", "Road Trip 2026");
    render(argv[1], "now_playing", &app, &status);
    {  // Made-up synced lyrics, the line at 83 s just begun (it slides in).
        static const char LRC[] = "[01:10.00]Under the harbor lights\n[01:16.00]we counted every boat\n"
                                  "[01:22.80]and the night was long\n[01:29.00]\n[01:31.00]so we sang along\n"
                                  "[01:37.00]to a song nobody wrote\n";
        if (lyricsParse(LRC, sizeof(LRC) - 1, &g_art.lyrics)) {
            g_art.has_lyrics = true;
            app.config.show_lyrics = true;
            render(argv[1], "now_playing_lyrics", &app, &status);
            app.config.show_lyrics = false;
            render(argv[1], "now_playing_lyrics_off", &app, &status);
            app.config.show_lyrics = true;
            lyricsFree(&g_art.lyrics);
            g_art.has_lyrics = false;
        }
        // Made-up plain lyrics (no times): the text moves along with the song.
        static const char PLAIN[] = "Under the paper moon\nwe counted every light\nthe river kept our names\n\n"
                                    "and carried them all night\nsing it slow, sing it low\nlet the morning find us\n";
        if (lyricsParse(PLAIN, sizeof(PLAIN) - 1, &g_art.lyrics)) {
            g_art.has_lyrics = true;
            render(argv[1], "now_playing_lyrics_plain", &app, &status);
            lyricsFree(&g_art.lyrics);
            g_art.has_lyrics = false;
        }
    }
    for (int i = 0; i < 6; i++)  // Turn the discs a little, so the previews show them turned.
        uiNowStyleSpin(true, 1000000000ull + i * 200000000ull);
    if (getenv("KAIPLAYER_BENCH")) {  // Milliseconds to draw a Now Playing frame, per style.
        static uint32_t frame[GFX_FB_WIDTH * GFX_FB_HEIGHT];
        gfxSetTarget(frame, GFX_FB_WIDTH);
        for (int style = 0; style < NOW_STYLE_COUNT; style++) {
            app.config.now_style = style;
            uiDraw(&app, &status, 0);  // Builds the cached layers.
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int i = 0; i < 60; i++)
                uiDraw(&app, &status, 0);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            printf("style %d: %.2f ms per frame\n", style,
                   ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 60.0);
        }
        app.config.now_style = NOW_STYLE_CD;  // With the Settings panel open over it.
        app.settings_open = true;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < 60; i++)
            uiDraw(&app, &status, 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        printf("CD with Settings open: %.2f ms per frame\n",
               ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 60.0);
        app.settings_open = false;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < 60; i++)
            gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, 0xFF000000u, 150);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        printf("full-screen dim alone: %.2f ms\n",
               ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 60.0);
        gfxSetTarget(NULL, 0);
    }
    static const char *const STYLE_SHOTS[] = {"", "now_playing_vinyl", "now_playing_cd", "now_playing_cassette"};
    for (int style = NOW_STYLE_VINYL; style < NOW_STYLE_COUNT; style++) {
        app.config.now_style = style;
        render(argv[1], STYLE_SHOTS[style], &app, &status);
    }
    // A background picture: the sample cover, as if it were in Music/Backgrounds.
    if (argc > 2) {
        char folder[512], cover[512];
        snprintf(folder, sizeof(folder), "%s/bg", argv[1]);
        mkdir(folder, 0777);
        snprintf(folder, sizeof(folder), "%s/bg/" BACKGROUND_FOLDER_NAME, argv[1]);
        mkdir(folder, 0777);
        snprintf(cover, sizeof(cover), "%s/cover.jpg", folder);
        FILE *in = fopen("assets/cover.jpg", "rb"), *out = fopen(cover, "wb");
        int ch;
        while (in && out && (ch = fgetc(in)) != EOF)
            fputc(ch, out);
        if (in) fclose(in);
        if (out) fclose(out);
        snprintf(folder, sizeof(folder), "%s/bg", argv[1]);
        if (backgroundLoad(folder, "cover.jpg", BLUR_MEDIUM)) {
            render(argv[1], "now_playing_background", &app, &status);
            app.config.now_style = NOW_STYLE_COVER;
            app.view = VIEW_QUEUE;
            render(argv[1], "queue_background", &app, &status);
            backgroundLoad(folder, "cover.jpg", BLUR_OFF);
            render(argv[1], "queue_background_sharp", &app, &status);
            backgroundLoad(folder, "cover.jpg", BLUR_HIGH);
            render(argv[1], "queue_background_high", &app, &status);
            app.view = VIEW_NOW_PLAYING;
            backgroundLoad(folder, "", BLUR_MEDIUM);
        }
    }
    for (int style = NOW_STYLE_VINYL; style < NOW_STYLE_COUNT; style++) {  // Under Settings: drawn, then kept.
        static const char *const FIRST[] = {"", "settings_vinyl", "settings_cd", "settings_cassette"};
        static const char *const KEPT[] = {"", "settings_vinyl_kept", "settings_cd_kept", "settings_cassette_kept"};
        app.config.now_style = style;
        app.settings_open = true;
        render(argv[1], FIRST[style], &app, &status);
        render(argv[1], KEPT[style], &app, &status);
        app.settings_open = false;
    }
    app.config.now_style = NOW_STYLE_COVER;
    app.playing_playlist[0] = '\0';
    status.repeat = REPEAT_ONE;
    status.shuffle = false;
    render(argv[1], "now_playing_repeat_one", &app, &status);
    status.repeat = REPEAT_ALL;
    status.shuffle = true;

    g_has_art = false;
    status.state = PLAYER_PAUSED;
    status.shuffle = false;
    status.track = 7;
    status.queue_pos = 3;
    app.sys.battery_percent = 12;
    render(argv[1], "now_playing_no_art", &app, &status);

    app.view = VIEW_BROWSER;
    app.browser.incomplete = true;
    app.sys.charging = true;
    render(argv[1], "browse_incomplete", &app, &status);
    app.browser.incomplete = false;
    renderPlaylistScenes(argv[1], &app, &status);
    renderBookScenes(argv[1], &app, &status);
    renderStreamScenes(argv[1], &app, &status);
    fontExit();
    if (g_touch_problems)
        printf("%d touch targets wouldn't answer a tap\n", g_touch_problems);
    return g_touch_problems ? 1 : 0;
}
