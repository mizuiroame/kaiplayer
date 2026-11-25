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
void testTouch(void);
void testEq(void);
void testMeta(void);
void testCatalog(void);
void testHome(void);
void testUpdate(void);
void testUpdateRelease(void);
void testNotice(void);
void testLyrics(void);
void testDsd(void);
void testStyles(void);
void testAppRoot(void);
void testKinetic(void);
void testLang(void);
void testKfile(void);
void testDav(void);
void testSubsonic(void);
void testFfaudio(void);
void testBooks(void);
