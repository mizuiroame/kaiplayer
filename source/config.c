#include "config.h"

#include "nowstyle.h"
#include "player.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CONFIG_NAME "settings.txt"
#define DEFAULT_FOLDER "sdmc:/"

static char g_folder[UTIL_PATH_MAX] = "sdmc:/config/kaiplayer";

void configSetFolder(const char *folder)
{
    snprintf(g_folder, sizeof(g_folder), "%s", folder);
}

const char *configFolder(void)
{
    return g_folder;
}

static int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// "0,3,-2,..." into the band gains; missing or bad values stay 0.
static void parseGains(const char *value, int gains[EQ_BANDS])
{
    const char *p = value;
    for (int b = 0; b < EQ_BANDS; b++) {
        char *end;
        long g = strtol(p, &end, 10);
        if (end == p)
            break;
        gains[b] = clampInt((int)g, EQ_GAIN_MIN, EQ_GAIN_MAX);
        p = *end == ',' ? end + 1 : end;
    }
}

static void applyLine(Config *c, char *line)
{
    line[strcspn(line, "\r\n")] = '\0';
    char *eq = strchr(line, '=');
    if (!eq)
        return;
    *eq = '\0';
    const char *key = line, *value = eq + 1;

    if (strcmp(key, "folder") == 0 && strlen(value) < sizeof(c->folder)) {
        strcpy(c->folder, value);
    } else if (strcmp(key, "library") == 0 && strlen(value) < sizeof(c->library)) {
        strcpy(c->library, value);
    } else if (strcmp(key, "library_source") == 0) {
        c->source = !strcmp(value, "network") ? LIBRARY_NETWORK : !strcmp(value, "server") ? LIBRARY_SERVER : LIBRARY_CARD;
    } else if (strcmp(key, "playlists_moved") == 0) {
        c->playlists_moved = strcmp(value, "1") == 0;
    } else if (strcmp(key, "shuffle") == 0) {
        c->shuffle = strcmp(value, "1") == 0;
    } else if (strcmp(key, "resume_mode") == 0) {
        int mode = atoi(value);
        if (mode >= 0 && mode < RESUME_MODE_COUNT)
            c->resume = (ResumeMode)mode;
    } else if (strcmp(key, "clock24") == 0) {
        c->clock_24h = strcmp(value, "0") != 0;
    } else if (strcmp(key, "eq_on") == 0) {
        c->eq_on = strcmp(value, "1") == 0;
    } else if (strcmp(key, "eq_preset") == 0) {
        c->eq_preset = clampInt(atoi(value), 0, EQ_PRESET_COUNT - 1);
    } else if (strcmp(key, "eq_gains") == 0) {
        parseGains(value, c->eq_gains);
    } else if (strcmp(key, "replaygain") == 0) {
        c->replaygain = clampInt(atoi(value), 0, REPLAYGAIN_MODE_COUNT - 1);
    } else if (strcmp(key, "accent") == 0) {
        c->accent = clampInt(atoi(value), 0, THEME_ACCENT_COUNT - 1);
    } else if (strcmp(key, "screen_off") == 0) {
        c->screen_off_minutes = clampInt(atoi(value), 0, 60);
    } else if (strcmp(key, "now_style") == 0) {
        c->now_style = clampInt(atoi(value), 0, NOW_STYLE_COUNT - 1);
    } else if (strcmp(key, "background") == 0 && strlen(value) < sizeof(c->background) && !strchr(value, '/')) {
        strcpy(c->background, value);
    } else if (strcmp(key, "update_check") == 0) {
        c->update_check = strcmp(value, "0") != 0;
    } else if (strcmp(key, "update_checked") == 0) {
        c->update_checked = strtoll(value, NULL, 10);
    } else if (strcmp(key, "update_tag") == 0 && strlen(value) < sizeof(c->update_tag) && !strpbrk(value, "/\\")) {
        strcpy(c->update_tag, value);
    } else if (strcmp(key, "language") == 0 && strlen(value) < sizeof(c->language) && !strpbrk(value, "/\\")) {
        strcpy(c->language, value);  // An id, or a name in the lang folder: never a path.
    } else if (strcmp(key, "background_blur") == 0) {
        c->background_blur = clampInt(atoi(value), 0, BLUR_COUNT - 1);
    } else if (strcmp(key, "repeat") == 0) {
        int r = atoi(value);
        if (r >= 0 && r < REPEAT_MODE_COUNT)
            c->repeat = (RepeatMode)r;
    }
}

void configLoad(Config *c)
{
    strcpy(c->folder, DEFAULT_FOLDER);
    c->library[0] = '\0';
    c->source = LIBRARY_CARD;
    c->shuffle = false;
    c->resume = RESUME_PLAYING;
    c->clock_24h = true;
    c->eq_on = false;
    c->eq_preset = 0;
    memset(c->eq_gains, 0, sizeof(c->eq_gains));
    c->replaygain = 0;
    c->accent = 0;
    c->screen_off_minutes = 0;
    c->now_style = NOW_STYLE_COVER;
    c->background[0] = '\0';
    c->background_blur = BLUR_MEDIUM;
    c->repeat = REPEAT_ALL;
    c->update_check = true;
    c->update_checked = 0;
    c->update_tag[0] = '\0';

    char path[UTIL_PATH_MAX];
    if (!utilJoinPath(path, sizeof(path), g_folder, CONFIG_NAME))
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[UTIL_PATH_MAX + 16];
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) {
            // Too long: drop the whole line so its tail isn't read as a new one.
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {
            }
            continue;
        }
        applyLine(c, line);
    }
    fclose(f);
}

bool configSave(const Config *c)
{
    char path[UTIL_PATH_MAX];
    if (!utilJoinPath(path, sizeof(path), g_folder, CONFIG_NAME))
        return false;
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
    // A line break in the folder would split it into extra settings.
    const char *folder = strpbrk(c->folder, "\r\n") ? DEFAULT_FOLDER : c->folder;
    fprintf(f, "folder=%s\nshuffle=%d\nrepeat=%d\nresume_mode=%d\nclock24=%d\n",
            folder, c->shuffle ? 1 : 0, (int)c->repeat, (int)c->resume, c->clock_24h ? 1 : 0);
    if (c->library[0] && !strpbrk(c->library, "\r\n"))
        fprintf(f, "library=%s\nplaylists_moved=%d\n", c->library, c->playlists_moved ? 1 : 0);
    if (c->source == LIBRARY_NETWORK || c->source == LIBRARY_SERVER)
        fprintf(f, "library_source=%s\n", c->source == LIBRARY_NETWORK ? "network" : "server");
    fprintf(f, "eq_on=%d\neq_preset=%d\neq_gains=", c->eq_on ? 1 : 0, c->eq_preset);
    for (int b = 0; b < EQ_BANDS; b++)
        fprintf(f, b ? ",%d" : "%d", c->eq_gains[b]);
    fprintf(f, "\nreplaygain=%d\naccent=%d\nscreen_off=%d\nnow_style=%d\nbackground_blur=%d\n", c->replaygain,
            c->accent, c->screen_off_minutes, c->now_style, c->background_blur);
    if (c->background[0] && !strpbrk(c->background, "\r\n/"))
        fprintf(f, "background=%s\n", c->background);
    if (c->language[0] && !strpbrk(c->language, "\r\n"))
        fprintf(f, "language=%s\n", c->language);
    fprintf(f, "update_check=%d\nupdate_checked=%lld\n", c->update_check ? 1 : 0, c->update_checked);
    if (c->update_tag[0] && !strpbrk(c->update_tag, "\r\n"))
        fprintf(f, "update_tag=%s\n", c->update_tag);
    return fclose(f) == 0;
}
