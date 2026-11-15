// Renders the real UI code into PPM images on the host, with a fake player
// and artwork, so the design can be checked without a console.
// Usage: preview OUTDIR [COVER.jpg]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "artwork.h"
#include "font.h"
#include "gfx.h"
#include "player.h"
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
        .state = PLAYER_PLAYING, .track = 2, .track_count = TRACK_COUNT,
        .position = 83.0, .duration = 247.0, .volume = 0.8f, .shuffle = true, .repeat = REPEAT_ALL,
    };
    loadArtwork(argc > 2 ? argv[2] : NULL);

    app.view = VIEW_BROWSER;
    render(argv[1], "browse", &app, &status);
    app.view = VIEW_QUEUE;
    app.queue_cursor = 4;
    render(argv[1], "queue", &app, &status);
    app.view = VIEW_NOW_PLAYING;
    render(argv[1], "now_playing", &app, &status);

    g_has_art = false;
    status.state = PLAYER_PAUSED;
    status.shuffle = false;
    status.track = 7;
    app.sys.battery_percent = 12;
    render(argv[1], "now_playing_no_art", &app, &status);

    app.view = VIEW_BROWSER;
    app.browser.incomplete = true;
    app.sys.charging = true;
    render(argv[1], "browse_incomplete", &app, &status);
    fontExit();
    return 0;
}
