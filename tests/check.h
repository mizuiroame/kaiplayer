// A minimal check macro shared by the host test files.
#pragma once

#include <stdio.h>

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        g_checks++;                                                                  \
        if (!(cond)) {                                                               \
            g_failures++;                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                            \
    } while (0)

#define TMP_ROOT "/tmp/kaiplayer_test"
#define FIXTURES "fixtures"

void testTags(void);
void testCover(void);
void testFixtures(void);
void testPlaylists(void);
