#include "cover.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "image_config.h"
#include "stb_image.h"
#include "util.h"

// Usual names of a folder's cover image, best first, without extension.
// "albumart" also matches Windows Media Player's AlbumArt_{...}_Large.jpg.
static const char *const COVER_NAMES[] = {"cover", "folder", "front", "albumart"};
#define COVER_NAME_COUNT (sizeof(COVER_NAMES) / sizeof(COVER_NAMES[0]))
#define PRIORITY_OTHER_IMAGE ((int)COVER_NAME_COUNT)  // Any other image file.
#define MAX_CANDIDATES 16
#define MAX_SCANNED_ENTRIES 4096

typedef struct {
    char name[256];
    int priority;
} CoverCandidate;

void coverFree(CoverImage *c)
{
    free(c->pixels);
    memset(c, 0, sizeof(*c));
}

static uint32_t packPixel(uint32_t r, uint32_t g, uint32_t b)
{
    return 0xFF000000u | b << 16 | g << 8 | r;
}

// Scales the centered square of an RGBA image to size x size by averaging
// the source pixels under each target pixel.
static bool scaleSquare(const uint8_t *rgba, int w, int h, int size, CoverImage *out)
{
    out->pixels = malloc((size_t)size * size * sizeof(uint32_t));
    if (!out->pixels)
        return false;
    out->size = size;
    int side = w < h ? w : h;
    int ox = (w - side) / 2, oy = (h - side) / 2;
    uint64_t total[3] = {0, 0, 0};

    for (int dy = 0; dy < size; dy++) {
        int y0 = oy + (int)((int64_t)dy * side / size);
        int y1 = oy + (int)((int64_t)(dy + 1) * side / size);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int dx = 0; dx < size; dx++) {
            int x0 = ox + (int)((int64_t)dx * side / size);
            int x1 = ox + (int)((int64_t)(dx + 1) * side / size);
            if (x1 <= x0)
                x1 = x0 + 1;
            uint64_t sum[3] = {0, 0, 0}, count = 0;
            for (int y = y0; y < y1; y++) {
                const uint8_t *p = rgba + ((size_t)y * w + x0) * 4;
                for (int x = x0; x < x1; x++, p += 4) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    count++;
                }
            }
            uint32_t r = (uint32_t)(sum[0] / count), g = (uint32_t)(sum[1] / count);
            uint32_t b = (uint32_t)(sum[2] / count);
            out->pixels[(size_t)dy * size + dx] = packPixel(r, g, b);
            total[0] += r;
            total[1] += g;
            total[2] += b;
        }
    }
    uint64_t n = (uint64_t)size * size;
    out->average = packPixel((uint32_t)(total[0] / n), (uint32_t)(total[1] / n), (uint32_t)(total[2] / n));
    return true;
}

bool coverDecode(const uint8_t *data, size_t size, CoverImage *large, int large_size,
                 CoverImage *small, int small_size)
{
    memset(large, 0, sizeof(*large));
    memset(small, 0, sizeof(*small));
    if (!data || size == 0 || size > COVER_FILE_MAX || large_size <= 0 || small_size <= 0)
        return false;

    // Check the header first so huge images are refused before decoding.
    int w, h, channels;
    if (!stbi_info_from_memory(data, (int)size, &w, &h, &channels) || w <= 0 || h <= 0 ||
        w > COVER_MAX_DIMENSION || h > COVER_MAX_DIMENSION)
        return false;

    uint8_t *rgba = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    if (!rgba)
        return false;
    bool ok = w > 0 && h > 0 && w <= COVER_MAX_DIMENSION && h <= COVER_MAX_DIMENSION &&
              scaleSquare(rgba, w, h, large_size, large) &&
              scaleSquare(rgba, w, h, small_size, small);
    stbi_image_free(rgba);
    if (!ok) {
        coverFree(large);
        coverFree(small);
    }
    return ok;
}

// Reads a whole file of at most COVER_FILE_MAX bytes. Caller frees.
static uint8_t *readSmallFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *data = NULL;
    long len = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        len = ftell(f);
    if (len > 0 && (unsigned long)len <= COVER_FILE_MAX && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t)len);
        if (data && fread(data, 1, (size_t)len, f) != (size_t)len) {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    *size = data ? (size_t)len : 0;
    return data;
}

// How good a file name is as a folder cover: 0 is best, -1 is not an image.
// Names are compared ignoring case: "Cover.JPG" is a cover.
int coverNamePriority(const char *name)
{
    if (!utilHasExtension(name, "jpg") && !utilHasExtension(name, "jpeg") && !utilHasExtension(name, "png"))
        return -1;
    const char *dot = strrchr(name, '.');
    size_t base = (size_t)(dot - name);
    for (size_t i = 0; i < COVER_NAME_COUNT; i++) {
        size_t n = strlen(COVER_NAMES[i]);
        bool prefix_ok = strcmp(COVER_NAMES[i], "albumart") == 0 ? base >= n : base == n;
        if (prefix_ok && strncasecmp(name, COVER_NAMES[i], n) == 0)
            return (int)i;
    }
    return PRIORITY_OTHER_IMAGE;
}

static int compareCandidates(const void *a, const void *b)
{
    return ((const CoverCandidate *)a)->priority - ((const CoverCandidate *)b)->priority;
}

// Lists the image files of a folder, best names first. An image with an
// unusual name is kept only when it is the folder's only image.
static size_t findCandidates(const char *folder, CoverCandidate *out)
{
    DIR *dir = opendir(folder);
    if (!dir)
        return 0;
    size_t count = 0, others = 0, scanned = 0;
    struct dirent *entry;
    while (scanned++ < MAX_SCANNED_ENTRIES && (entry = readdir(dir)) != NULL) {
        int priority = coverNamePriority(entry->d_name);
        if (priority < 0 || entry->d_name[0] == '.' || strlen(entry->d_name) >= sizeof(out->name) ||
            utilEntryIsDir(folder, entry))
            continue;
        others += priority == PRIORITY_OTHER_IMAGE;
        if (count < MAX_CANDIDATES) {
            snprintf(out[count].name, sizeof(out[count].name), "%s", entry->d_name);
            out[count++].priority = priority;
        }
    }
    closedir(dir);
    qsort(out, count, sizeof(CoverCandidate), compareCandidates);
    while (count > 0 && others > 1 && out[count - 1].priority == PRIORITY_OTHER_IMAGE)
        count--;
    return count;
}

bool coverLoadFromFolder(const char *track_path, CoverImage *large, int large_size,
                         CoverImage *small, int small_size)
{
    char folder[UTIL_PATH_MAX], path[UTIL_PATH_MAX];
    snprintf(folder, sizeof(folder), "%s", track_path);
    if (!utilParentPath(folder))
        return false;
    CoverCandidate candidates[MAX_CANDIDATES];
    size_t count = findCandidates(folder, candidates);
    for (size_t i = 0; i < count; i++) {
        if (!utilJoinPath(path, sizeof(path), folder, candidates[i].name))
            continue;
        size_t size = 0;
        uint8_t *data = readSmallFile(path, &size);
        if (!data)
            continue;
        bool ok = coverDecode(data, size, large, large_size, small, small_size);
        free(data);
        if (ok)
            return true;
    }
    return false;
}
