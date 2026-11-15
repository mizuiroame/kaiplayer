#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CONFIG_DIR "sdmc:/config/kaiplayer"
#define CONFIG_FILE CONFIG_DIR "/settings.txt"
#define DEFAULT_FOLDER "sdmc:/"

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
    } else if (strcmp(key, "shuffle") == 0) {
        c->shuffle = strcmp(value, "1") == 0;
    } else if (strcmp(key, "repeat") == 0) {
        int r = atoi(value);
        if (r >= 0 && r < REPEAT_MODE_COUNT)
            c->repeat = (RepeatMode)r;
    }
}

void configLoad(Config *c)
{
    strcpy(c->folder, DEFAULT_FOLDER);
    c->shuffle = false;
    c->repeat = REPEAT_ALL;

    FILE *f = fopen(CONFIG_FILE, "r");
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
    mkdir("sdmc:/config", 0777);
    mkdir(CONFIG_DIR, 0777);
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f)
        return false;
    // A line break in the folder would split it into extra settings.
    const char *folder = strpbrk(c->folder, "\r\n") ? DEFAULT_FOLDER : c->folder;
    fprintf(f, "folder=%s\nshuffle=%d\nrepeat=%d\n",
            folder, c->shuffle ? 1 : 0, (int)c->repeat);
    return fclose(f) == 0;
}
