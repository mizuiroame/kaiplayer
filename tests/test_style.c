// Tests for the background picture and the Now Playing style names.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "background.h"
#include "check.h"
#include "nowstyle.h"

#define BASE 0xFF141212u

static void testMake(void)
{
    // A plain red picture comes out as red mixed into the dark base.
    uint8_t red[4 * 2 * 4];
    for (int i = 0; i < 8; i++)
        memcpy(red + i * 4, (uint8_t[]){255, 0, 0, 255}, 4);
    uint32_t out[64 * 36];
    CHECK(backgroundMake(red, 4, 2, out, 64, 36, BASE, 255));
    CHECK(out[0] == 0xFF0000FFu && out[64 * 36 - 1] == 0xFF0000FFu);
    CHECK(backgroundMake(red, 4, 2, out, 64, 36, BASE, 0));
    CHECK(out[100] == BASE);

    // Black on the left, white on the right: still dark left and light right, blurred between.
    static uint8_t split[200 * 100 * 4];
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 200; x++)
            memset(split + (y * 200 + x) * 4, x < 100 ? 0 : 255, 4);
    CHECK(backgroundMake(split, 200, 100, out, 64, 36, 0xFF000000u, 255));
    uint32_t left = out[18 * 64] & 0xFF, right = out[18 * 64 + 63] & 0xFF, middle = out[18 * 64 + 32] & 0xFF;
    CHECK(left < 40 && right > 215 && middle > 60 && middle < 195);
}

static void touchFile(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f)
        fclose(f);
}

static void testList(void)
{
    const char *library = TMP_ROOT "/bglib";
    mkdir(library, 0777);
    char names[8][BACKGROUND_NAME_MAX];
    CHECK(backgroundList(library, names, 8) == 0);  // No Backgrounds folder.
    mkdir(TMP_ROOT "/bglib/" BACKGROUND_FOLDER_NAME, 0777);
    touchFile(TMP_ROOT "/bglib/Backgrounds/b.png");
    touchFile(TMP_ROOT "/bglib/Backgrounds/A.JPG");
    touchFile(TMP_ROOT "/bglib/Backgrounds/notes.txt");
    touchFile(TMP_ROOT "/bglib/Backgrounds/.hidden.png");
    CHECK(backgroundList(library, names, 8) == 2);
    CHECK(strcmp(names[0], "A.JPG") == 0 && strcmp(names[1], "b.png") == 0);
    CHECK(backgroundList("", names, 8) == 0);

    CHECK(!backgroundLoad(library, "b.png") && backgroundPixels() == NULL);  // Empty: not an image.
    CHECK(backgroundLoad(library, "") && backgroundPixels() == NULL);        // The cover colors.
    FILE *in = fopen(FIXTURES "/small_cover.png", "rb"), *out = fopen(TMP_ROOT "/bglib/Backgrounds/c.png", "wb");
    int ch;
    while (in && out && (ch = fgetc(in)) != EOF)
        fputc(ch, out);
    if (in) fclose(in);
    if (out) fclose(out);
    CHECK(backgroundLoad(library, "c.png") && backgroundPixels() != NULL);
    CHECK(backgroundLoad(library, "") && backgroundPixels() == NULL);
}

void testStyles(void)
{
    testMake();
    testList();
    CHECK(strcmp(nowStyleName(NOW_STYLE_VINYL), "Vinyl record") == 0);
    CHECK(strcmp(nowStyleName(99), "Cover") == 0);
}
