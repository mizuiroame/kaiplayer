// Renders the real UI code into PPM images on the host, with a fake player
// and artwork, so the design can be checked without a console.
// Usage: preview OUTDIR [COVER.jpg]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "artwork.h"
#include "font.h"
#include "meta.h"
#include "gfx.h"
#include "player.h"
#include "theme.h"
#include "ui.h"

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
bool metaLookup(const char *path, SongMeta *out)
{
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
    fprintf(f, "P6\n%d %d\n255\n", GFX_WIDTH, GFX_HEIGHT);
    for (int i = 0; i < GFX_WIDTH * GFX_HEIGHT; i++) {
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
    static uint32_t pixels[GFX_WIDTH * GFX_HEIGHT];
    char path[512];
    gfxSetTarget(pixels, GFX_WIDTH);
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
    g_art.has_cover = cover_path && coverLoadFromFolder(cover_path, &g_art.cover, ARTWORK_LARGE_SIZE,
                                                         &g_art.thumb, ARTWORK_SMALL_SIZE);
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
    render(argv[1], "browse", &app, &status);
    app.focus = FOCUS_ADD;
    render(argv[1], "browse_focus_add", &app, &status);
    app.focus = FOCUS_LIST;
    app.view = VIEW_QUEUE;
    app.queue_cursor = 3;
    render(argv[1], "queue", &app, &status);
    app.focus = FOCUS_PLAYER;
    app.bar_control = BAR_NEXT;
    render(argv[1], "player_bar_focus", &app, &status);
    app.focus = FOCUS_LIST;
    app.view = VIEW_NOW_PLAYING;
    render(argv[1], "now_playing", &app, &status);
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
