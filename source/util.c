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

bool utilHasExtension(const char *path, const char *ext)
{
    const char *dot = strrchr(utilFileName(path), '.');
    return dot && utilCompareNames(dot + 1, ext) == 0;
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

bool utilPathWithin(const char *path, const char *folder)
{
    size_t len = strlen(folder);
    while (len > 0 && folder[len - 1] == '/')
        len--;  // "sdmc:/music/" and "sdmc:/" compare without the slash.
    if (strncmp(path, folder, len) != 0)
        return false;
    return path[len] == '\0' || path[len] == '/';
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

// Length of a device prefix such as "sdmc:/", or 0 if the path has none.
static size_t devicePrefix(const char *path)
{
    const char *colon = strstr(path, ":/");
    return colon && !memchr(path, '/', (size_t)(colon - path)) ? (size_t)(colon - path) + 2 : 0;
}

bool utilNormalizePath(char *path)
{
    size_t prefix = devicePrefix(path);
    bool absolute = prefix > 0 || path[0] == '/';
    if (!prefix && absolute)
        prefix = 1;
    char *out = path + prefix;  // Written components, separated by '/'.
    size_t len = 0;
    const char *p = path + prefix;
    while (*p) {
        while (*p == '/')
            p++;
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == 0)
            break;
        if (n == 1 && p[0] == '.') {
            // Skip.
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            if (len == 0)
                return false;
            while (len > 0 && out[len - 1] != '/')
                len--;
            if (len > 0)
                len--;  // The slash before the dropped component.
        } else {
            if (len > 0)
                out[len++] = '/';
            memmove(out + len, p, n);
            len += n;
        }
        p += n;
    }
    out[len] = '\0';
    return true;
}

bool utilResolvePath(const char *base_dir, const char *ref, char *out, size_t size)
{
    char clean[UTIL_PATH_MAX];
    int n = snprintf(clean, sizeof(clean), "%s", ref);
    if (n < 0 || (size_t)n >= sizeof(clean))
        return false;
    for (char *c = clean; *c; c++) {
        if (*c == '\\')
            *c = '/';
    }

    int written;
    if (devicePrefix(clean)) {
        written = snprintf(out, size, "%s", clean);
    } else if (clean[0] == '/') {
        size_t prefix = devicePrefix(base_dir);
        written = snprintf(out, size, "%.*s%s", (int)(prefix ? prefix - 1 : 0), base_dir, clean);
    } else {
        written = utilJoinPath(out, size, base_dir, clean) ? 0 : -1;
    }
    return written >= 0 && (size_t)written < size && utilNormalizePath(out);
}

// Splits a normalized path after its device prefix into components.
static int splitComponents(const char *path, const char **parts, size_t *lens, int max)
{
    const char *p = path + devicePrefix(path);
    int count = 0;
    while (*p && count < max) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        parts[count] = p;
        lens[count++] = n;
        p += n;
        if (*p == '/')
            p++;
    }
    return count;
}

#define MAX_COMPONENTS 64

bool utilRelativePath(const char *from_dir, const char *target, char *out, size_t size)
{
    size_t from_prefix = devicePrefix(from_dir), target_prefix = devicePrefix(target);
    if (from_prefix != target_prefix || strncmp(from_dir, target, from_prefix) != 0) {
        int n = snprintf(out, size, "%s", target);
        return n >= 0 && (size_t)n < size;
    }
    const char *a[MAX_COMPONENTS], *b[MAX_COMPONENTS];
    size_t alen[MAX_COMPONENTS], blen[MAX_COMPONENTS];
    int na = splitComponents(from_dir, a, alen, MAX_COMPONENTS);
    int nb = splitComponents(target, b, blen, MAX_COMPONENTS);
    int common = 0;
    while (common < na && common < nb && alen[common] == blen[common] &&
           strncmp(a[common], b[common], alen[common]) == 0)
        common++;

    size_t pos = 0;
    out[0] = '\0';
    for (int i = common; i < na; i++) {
        if (pos + 3 >= size)
            return false;
        memcpy(out + pos, "../", 3);
        pos += 3;
    }
    for (int i = common; i < nb; i++) {
        size_t need = blen[i] + (i + 1 < nb ? 1 : 0);
        if (pos + need >= size)
            return false;
        memcpy(out + pos, b[i], blen[i]);
        pos += blen[i];
        if (i + 1 < nb)
            out[pos++] = '/';
    }
    out[pos] = '\0';
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

unsigned utilNextCodePoint(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned cp;
    int extra;
    if (p[0] < 0x80) {
        cp = p[0];
        extra = 0;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        extra = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        extra = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        extra = 3;
    } else {
        *s += 1;
        return '?';
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {  // Also stops at the NUL terminator.
            *s += i;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += extra + 1;
    return cp;
}

void utilFormatClock(char *out, size_t size, int hour, int minute, bool use_24h)
{
    hour = ((hour % 24) + 24) % 24;
    minute = ((minute % 60) + 60) % 60;
    if (use_24h) {
        snprintf(out, size, "%02d:%02d", hour, minute);
        return;
    }
    int h12 = hour % 12 == 0 ? 12 : hour % 12;
    snprintf(out, size, "%d:%02d %s", h12, minute, hour < 12 ? "AM" : "PM");
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
