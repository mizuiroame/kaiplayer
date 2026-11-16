#include "background.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cover.h"
#include "gfx.h"
#include "image_config.h"
#include "stb_image.h"
#include "util.h"

#define FILE_MAX (16u << 20)
#define SMALL_DIVISOR 4      // Blurred at a quarter of the screen size, then scaled up: smooth and quick.
#define BLUR_RADIUS 5
#define BLUR_PASSES 3        // Three box blurs look like a Gaussian one.
#define IMAGE_WEIGHT 90      // Of 255: how much of the picture shows through the dark base.
#define BASE_COLOR 0xFF141212u  // COLOR_BG (0x12, 0x12, 0x14).

static uint32_t *g_frame;
static bool g_loaded;

const uint32_t *backgroundPixels(void)
{
    return g_loaded ? g_frame : NULL;
}

static bool isImageName(const char *name)
{
    return name[0] != '.' && (utilHasExtension(name, "jpg") || utilHasExtension(name, "jpeg") ||
                              utilHasExtension(name, "png"));
}

static int compareNames(const void *a, const void *b)
{
    return utilCompareNames((const char *)a, (const char *)b);
}

size_t backgroundList(const char *library, char names[][BACKGROUND_NAME_MAX], size_t max)
{
    char folder[UTIL_PATH_MAX];
    if (!library[0] || !utilJoinPath(folder, sizeof(folder), library, BACKGROUND_FOLDER_NAME))
        return 0;
    DIR *dir = opendir(folder);
    if (!dir)
        return 0;
    size_t count = 0;
    struct dirent *entry;
    while (count < max && (entry = readdir(dir)) != NULL) {
        size_t n = strlen(entry->d_name);
        if (isImageName(entry->d_name) && n < BACKGROUND_NAME_MAX)
            memcpy(names[count++], entry->d_name, n + 1);
    }
    closedir(dir);
    qsort(names, count, BACKGROUND_NAME_MAX, compareNames);
    return count;
}

// Box-filters the centered crop of the image that has the out size's shape.
static void shrinkToFill(const uint8_t *rgba, int w, int h, float *out, int ow, int oh)
{
    double scale = (double)ow / w > (double)oh / h ? (double)ow / w : (double)oh / h;
    double cw = ow / scale, ch = oh / scale, cx = (w - cw) / 2.0, cy = (h - ch) / 2.0;
    for (int y = 0; y < oh; y++) {
        int y0 = (int)(cy + y * ch / oh), y1 = (int)(cy + (y + 1) * ch / oh);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > h) y1 = h;
        for (int x = 0; x < ow; x++) {
            int x0 = (int)(cx + x * cw / ow), x1 = (int)(cx + (x + 1) * cw / ow);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > w) x1 = w;
            float sum[3] = {0.0f, 0.0f, 0.0f};
            for (int sy = y0; sy < y1; sy++) {
                const uint8_t *p = rgba + ((size_t)sy * w + x0) * 4;
                for (int sx = x0; sx < x1; sx++, p += 4)
                    sum[0] += p[0], sum[1] += p[1], sum[2] += p[2];
            }
            float n = (float)((y1 - y0) * (x1 - x0));
            for (int c = 0; c < 3; c++)
                out[((size_t)y * ow + x) * 3 + c] = sum[c] / n;
        }
    }
}

// One box blur along rows (step 3) or columns (step 3 * width), edges held.
static void boxBlur(float *img, float *tmp, int count, int length, size_t step, size_t next)
{
    for (int line = 0; line < count; line++) {
        float *base = img + line * next;
        for (int c = 0; c < 3; c++) {
            float sum = 0.0f;
            for (int i = -BLUR_RADIUS; i <= BLUR_RADIUS; i++)
                sum += base[(size_t)(i < 0 ? 0 : i >= length ? length - 1 : i) * step + c];
            for (int i = 0; i < length; i++) {
                tmp[i] = sum / (2 * BLUR_RADIUS + 1);
                int out = i - BLUR_RADIUS, in = i + BLUR_RADIUS + 1;
                sum += base[(size_t)(in >= length ? length - 1 : in) * step + c] -
                       base[(size_t)(out < 0 ? 0 : out) * step + c];
            }
            for (int i = 0; i < length; i++)
                base[(size_t)i * step + c] = tmp[i];
        }
    }
}

bool backgroundMake(const uint8_t *rgba, int w, int h, uint32_t *out, int out_w, int out_h, uint32_t base,
                    unsigned dim)
{
    int sw = out_w / SMALL_DIVISOR, sh = out_h / SMALL_DIVISOR;
    float *small = malloc((size_t)sw * sh * 3 * sizeof(float));
    float *tmp = malloc((size_t)(sw > sh ? sw : sh) * sizeof(float));
    if (!small || !tmp || sw < 2 || sh < 2) {
        free(small);
        free(tmp);
        return false;
    }
    shrinkToFill(rgba, w, h, small, sw, sh);
    for (int pass = 0; pass < BLUR_PASSES; pass++) {
        boxBlur(small, tmp, sh, sw, 3, (size_t)sw * 3);
        boxBlur(small, tmp, sw, sh, (size_t)sw * 3, 3);
    }
    float keep = dim / 255.0f, br = (float)(base & 0xFF), bg = (float)(base >> 8 & 0xFF),
          bb = (float)(base >> 16 & 0xFF);
    for (int y = 0; y < out_h; y++) {
        float fy = (y + 0.5f) / SMALL_DIVISOR - 0.5f;
        int y0 = fy < 0.0f ? 0 : (int)fy, y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
        float ty = fy < 0.0f ? 0.0f : fy - y0;
        for (int x = 0; x < out_w; x++) {
            float fx = (x + 0.5f) / SMALL_DIVISOR - 0.5f;
            int x0 = fx < 0.0f ? 0 : (int)fx, x1 = x0 + 1 < sw ? x0 + 1 : sw - 1;
            float tx = fx < 0.0f ? 0.0f : fx - x0;
            const float *a = small + ((size_t)y0 * sw + x0) * 3, *b = small + ((size_t)y0 * sw + x1) * 3;
            const float *c = small + ((size_t)y1 * sw + x0) * 3, *d = small + ((size_t)y1 * sw + x1) * 3;
            float v[3];
            for (int k = 0; k < 3; k++) {
                float top = a[k] + (b[k] - a[k]) * tx, bottom = c[k] + (d[k] - c[k]) * tx;
                v[k] = top + (bottom - top) * ty;
            }
            uint32_t r = (uint32_t)(br + (v[0] - br) * keep), g = (uint32_t)(bg + (v[1] - bg) * keep),
                     bl = (uint32_t)(bb + (v[2] - bb) * keep);
            out[(size_t)y * out_w + x] = 0xFF000000u | bl << 16 | g << 8 | r;
        }
    }
    free(small);
    free(tmp);
    return true;
}

static uint8_t *readFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *data = NULL;
    long len = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        len = ftell(f);
    if (len > 0 && (unsigned long)len <= FILE_MAX && fseek(f, 0, SEEK_SET) == 0) {
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

bool backgroundLoad(const char *library, const char *name)
{
    g_loaded = false;
    char folder[UTIL_PATH_MAX], path[UTIL_PATH_MAX];
    if (!name[0])
        return true;
    if (!utilJoinPath(folder, sizeof(folder), library, BACKGROUND_FOLDER_NAME) ||
        !utilJoinPath(path, sizeof(path), folder, name))
        return false;
    size_t size = 0;
    uint8_t *data = readFile(path, &size);
    int w = 0, h = 0, channels = 0;
    bool ok = data && stbi_info_from_memory(data, (int)size, &w, &h, &channels) && w > 0 && h > 0 &&
              w <= COVER_MAX_DIMENSION && h <= COVER_MAX_DIMENSION;
    uint8_t *rgba = ok ? stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4) : NULL;
    free(data);
    if (rgba && !g_frame)
        g_frame = malloc((size_t)GFX_FB_WIDTH * GFX_FB_HEIGHT * sizeof(uint32_t));
    ok = rgba && g_frame &&
         backgroundMake(rgba, w, h, g_frame, GFX_FB_WIDTH, GFX_FB_HEIGHT, BASE_COLOR, IMAGE_WEIGHT);
    if (rgba)
        stbi_image_free(rgba);
    g_loaded = ok;
    return ok;
}
