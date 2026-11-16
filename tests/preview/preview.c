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
#include "catalog_scan.h"
#include "covercache.h"
#include "font.h"
#include "meta.h"
#include "gfx.h"
#include "player.h"
#include "theme.h"
#include "ui.h"
#include "ui_nowstyle.h"
#include "nowstyle.h"

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
bool playerTrackPath(size_t track, char *out, size_t size)
{
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
    // The sample cover stands in for every album that has one.
    return g_art.has_cover && strstr(song_path, "Aurora") ? (thumb ? &g_art_thumb_small : &g_art_card) : NULL;
}

bool catalogScanProgress(size_t *done, size_t *total)
{
    *done = 312;
    *total = 480;
    return g_scan_busy;
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

static void render(const char *dir, const char *name, const App *app, const PlayerStatus *status)
{
    static uint32_t pixels[GFX_FB_WIDTH * GFX_FB_HEIGHT];
    char path[512];
    gfxSetTarget(pixels, GFX_FB_WIDTH);
    uiDraw(app, status, 0);
    gfxSetTarget(NULL, 0);
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

    menuOpen(&app->menu, "Menu");
    menuAdd(&app->menu, "Move song", 0, 0);
    menuAdd(&app->menu, "Remove song from playlist", 0, 0);
    menuAdd(&app->menu, "Rename \"Road Trip 2026\"", 0, 0);
    menuAdd(&app->menu, "Delete \"Road Trip 2026\"", 0, 0);
    menuAdd(&app->menu, "Exit KaiPlayer", 0, 0);
    app->menu.cursor = 1;
    render(dir, "menu", app, status);

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
    app->settings_cursor = SETTING_SLEEP;
    render(dir, "settings", app, status);
    app->about_open = true;
    render(dir, "about", app, status);
    app->about_open = false;
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

int main(int argc, char **argv)
{
    if (argc < 2 || !fontInit()) {
        fprintf(stderr, "usage: preview OUTDIR [TRACK_IN_COVER_FOLDER]\n");
        return 2;
    }
    static App app;
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
    app.library.stack[0].level = LIB_FOLDERS;  // The rest of the scenes show Folders.
    app.library.depth = 1;

    // The welcome screen: choosing the music folder.
    snprintf(app.picker.path, sizeof(app.picker.path), "%s", "sdmc:/");
    addEntry(&app.picker, "Music", true);
    addEntry(&app.picker, "Podcasts", true);
    addEntry(&app.picker, "Nintendo", true);
    addEntry(&app.picker, "switch", true);
    addEntry(&app.picker, "notes.txt", false);
    app.picker_open = app.picker_first_run = true;
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
        if (backgroundLoad(folder, "cover.jpg")) {
            render(argv[1], "now_playing_background", &app, &status);
            app.config.now_style = NOW_STYLE_COVER;
            app.view = VIEW_QUEUE;
            render(argv[1], "queue_background", &app, &status);
            app.view = VIEW_NOW_PLAYING;
            backgroundLoad(folder, "");
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
    fontExit();
    return 0;
}
