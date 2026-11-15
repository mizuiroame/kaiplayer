#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *const AUDIO_EXTENSIONS[] = {"mp3", "flac", "ogg", "oga", "wav"};

bool utilIsAudioFile(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name)
        return false;
    for (size_t i = 0; i < sizeof(AUDIO_EXTENSIONS) / sizeof(AUDIO_EXTENSIONS[0]); i++) {
        if (utilCompareNames(dot + 1, AUDIO_EXTENSIONS[i]) == 0)
            return true;
    }
    return false;
}

bool utilEntryIsDir(const char *folder, const struct dirent *entry)
{
#ifdef DT_DIR
    if (entry->d_type == DT_DIR)
        return true;
    if (entry->d_type != DT_UNKNOWN)
        return false;
#endif
    char path[UTIL_PATH_MAX];
    struct stat st;
    return utilJoinPath(path, sizeof(path), folder, entry->d_name) &&
           stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

const char *utilFileName(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool utilJoinPath(char *out, size_t size, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    const char *sep = (len > 0 && dir[len - 1] == '/') ? "" : "/";
    int written = snprintf(out, size, "%s%s%s", dir, sep, name);
    return written >= 0 && (size_t)written < size;
}

bool utilParentPath(char *path)
{
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/' && path[len - 2] != ':')
        path[--len] = '\0';

    char *slash = strrchr(path, '/');
    if (!slash || slash[1] == '\0')
        return false;

    // Keep the slash after a device name ("sdmc:/").
    if (slash > path && slash[-1] == ':')
        slash[1] = '\0';
    else
        slash[0] = '\0';
    return true;
}

int utilCompareNames(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb || ca == '\0')
            return ca - cb;
    }
}

void utilFormatTime(char *out, size_t size, double seconds)
{
    if (seconds < 0)
        seconds = 0;
    unsigned total = (unsigned)seconds;
    unsigned h = total / 3600, m = (total / 60) % 60, s = total % 60;
    if (h > 0)
        snprintf(out, size, "%u:%02u:%02u", h, m, s);
    else
        snprintf(out, size, "%u:%02u", m, s);
}
