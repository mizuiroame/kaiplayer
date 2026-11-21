#include "cover.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "image_config.h"
#include "kfile.h"
#include "stb_image.h"
#include "subsonic.h"
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
    out->vibrant = coverVibrantColor(out->pixels, (size_t)n);
    return true;
}

// Picks the most common vivid hue of the scaled image. Averaging the whole
// cover mixes its colors into grey; this keeps the one that stands out.
#define HUE_BINS 24
#define MIN_VIVID_SHARE 0.03  // Of the image's pixels, weighted by vividness.

uint32_t coverVibrantColor(const uint32_t *pixels, size_t count)
{
    double weight[HUE_BINS] = {0}, sum[HUE_BINS][3] = {{0}};
    for (size_t i = 0; i < count; i++) {
        int r = (int)(pixels[i] & 0xFF), g = (int)((pixels[i] >> 8) & 0xFF), b = (int)((pixels[i] >> 16) & 0xFF);
        int max = r > g ? (r > b ? r : b) : (g > b ? g : b), min = r < g ? (r < b ? r : b) : (g < b ? g : b);
        int delta = max - min;
        if (max < 40 || delta * 4 < max)  // Too dark, or saturation under 25%.
            continue;
        double h = max == r ? (double)(g - b) / delta : max == g ? 2.0 + (double)(b - r) / delta
                                                                 : 4.0 + (double)(r - g) / delta;
        if (h < 0.0)
            h += 6.0;
        int bin = (int)(h / 6.0 * HUE_BINS) % HUE_BINS;
        double w = (double)delta / 255.0 * (double)max / 255.0;  // Saturation x value, roughly.
        weight[bin] += w;
        sum[bin][0] += w * r, sum[bin][1] += w * g, sum[bin][2] += w * b;
    }
    int best = 0;
    for (int i = 1; i < HUE_BINS; i++)
        best = weight[i] > weight[best] ? i : best;
    if (count == 0 || weight[best] < MIN_VIVID_SHARE * (double)count)
        return 0;
    return packPixel((uint32_t)(sum[best][0] / weight[best]), (uint32_t)(sum[best][1] / weight[best]),
                     (uint32_t)(sum[best][2] / weight[best]));
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

// Reads a whole file of at most COVER_FILE_MAX bytes, on the card or the network. Caller frees.
static uint8_t *readSmallFile(const char *path, size_t *size)
{
    uint8_t *data = kfileReadAll(path, COVER_FILE_MAX, size, NULL);
    if (data && *size == 0) {
        free(data);
        data = NULL;
    }
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

// Keeps an image file's name if there's room. *others counts the unusual names.
static void addCandidate(const char *name, CoverCandidate *out, size_t *count, size_t *others)
{
    int priority = coverNamePriority(name);
    if (priority < 0 || name[0] == '.' || strlen(name) >= sizeof(out->name))
        return;
    *others += priority == PRIORITY_OTHER_IMAGE;
    if (*count < MAX_CANDIDATES) {
        snprintf(out[*count].name, sizeof(out[*count].name), "%s", name);
        out[(*count)++].priority = priority;
    }
}

// A network folder's images, from its listing.
static void findNetworkCandidates(const char *folder, CoverCandidate *out, size_t *count, size_t *others)
{
    DavEntry *entries;
    size_t n;
    char why[160];
    if (!kfileList(folder, &entries, &n, why, sizeof(why)))
        return;
    for (size_t i = 0; i < n && i < MAX_SCANNED_ENTRIES; i++)
        if (!entries[i].is_dir)
            addCandidate(entries[i].name, out, count, others);
    free(entries);
}

// Lists the image files of a folder, best names first. An image with an
// unusual name is kept only when it is the folder's only image.
static size_t findCandidates(const char *folder, CoverCandidate *out)
{
    size_t count = 0, others = 0, scanned = 0;
    if (kfileIsRemote(folder)) {
        findNetworkCandidates(folder, out, &count, &others);
    } else {
        DIR *dir = opendir(folder);
        if (!dir)
            return 0;
        struct dirent *entry;
        while (scanned++ < MAX_SCANNED_ENTRIES && (entry = readdir(dir)) != NULL) {
            if (coverNamePriority(entry->d_name) >= 0 && !utilEntryIsDir(folder, entry))
                addCandidate(entry->d_name, out, &count, &others);
        }
        closedir(dir);
    }
    qsort(out, count, sizeof(CoverCandidate), compareCandidates);
    while (count > 0 && others > 1 && out[count - 1].priority == PRIORITY_OTHER_IMAGE)
        count--;
    return count;
}

// A server's song: its album's cover, from the server, at the size it's drawn.
static bool loadServerCover(const char *track_path, CoverImage *large, int large_size, CoverImage *small,
                            int small_size)
{
    char location[UTIL_PATH_MAX];
    size_t size = 0;
    uint8_t *data = subCoverLocation(track_path, large_size, location, sizeof(location))
                        ? readSmallFile(location, &size)
                        : NULL;
    bool ok = data && coverDecode(data, size, large, large_size, small, small_size);
    free(data);
    return ok;
}

bool coverLoadFromFolder(const char *track_path, CoverImage *large, int large_size,
                         CoverImage *small, int small_size)
{
    if (subIsLocation(track_path))
        return loadServerCover(track_path, large, large_size, small, small_size);
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
