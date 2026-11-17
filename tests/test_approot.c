// The app's own folder next to its NRO, and moving the old one's files there.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "approot.h"
#include "check.h"

static bool folderIs(const char *nro, const char *expected, bool expected_ok)
{
    char out[APPROOT_PATH_MAX];
    bool ok = appRootFolder(nro, "kaiplayer", out, sizeof(out));
    return ok == expected_ok && strcmp(out, expected) == 0;
}

static void writeText(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static bool readIs(const char *path, const char *text)
{
    char buf[64] = "";
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strcmp(buf, text) == 0;
}

void testAppRoot(void)
{
    CHECK(folderIs("sdmc:/brewstore/apps/kaiplayer/KaiPlayer.nro", "sdmc:/brewstore/apps/kaiplayer/config", true));
    CHECK(folderIs("sdmc:/switch/KaiPlayer/KaiPlayer.nro", "sdmc:/switch/KaiPlayer/config", true));  // Any case.
    CHECK(folderIs("sdmc:/switch/KaiPlayer.nro", "sdmc:/switch/kaiplayer/config", true));  // Never loose in /switch.
    CHECK(folderIs("sdmc:/KaiPlayer.nro", "sdmc:/kaiplayer/config", true));
    CHECK(folderIs("sdmc:/switch/apps/KaiPlayer.nro", "sdmc:/switch/apps/kaiplayer/config", true));
    // No usable path (launched over the network, or none): the old folder.
    CHECK(folderIs(NULL, "sdmc:/config/kaiplayer", false));
    CHECK(folderIs("", "sdmc:/config/kaiplayer", false));
    CHECK(folderIs("KaiPlayer.nro", "sdmc:/config/kaiplayer", false));
    CHECK(folderIs("sdmc:/switch/../x/KaiPlayer.nro", "sdmc:/config/kaiplayer", false));

    // Making the folders, then moving: what's there stays, the rest is copied, once.
    const char *old = TMP_ROOT "/old/kaiplayer", *now = TMP_ROOT "/switch/kaiplayer/config";
    CHECK(appRootMakeFolder(old) && appRootMakeFolder(TMP_ROOT "/old/kaiplayer/Backgrounds"));
    writeText(TMP_ROOT "/old/kaiplayer/resume.txt", "old resume");
    writeText(TMP_ROOT "/old/kaiplayer/settings.txt", "old settings");
    writeText(TMP_ROOT "/old/kaiplayer/Backgrounds/sea.png", "png");
    CHECK(appRootMakeFolder(now));
    writeText(TMP_ROOT "/switch/kaiplayer/config/settings.txt", "new settings");
    CHECK(appRootMigrate(old, now) == 2);
    CHECK(readIs(TMP_ROOT "/switch/kaiplayer/config/resume.txt", "old resume"));
    CHECK(readIs(TMP_ROOT "/switch/kaiplayer/config/settings.txt", "new settings"));  // Not overwritten.
    CHECK(readIs(TMP_ROOT "/switch/kaiplayer/config/Backgrounds/sea.png", "png"));
    CHECK(readIs(TMP_ROOT "/old/kaiplayer/resume.txt", "old resume"));  // The old folder is left.
    // Once: a file added to the old folder later isn't copied.
    writeText(TMP_ROOT "/old/kaiplayer/later.txt", "later");
    CHECK(appRootMigrate(old, now) == 0);
    struct stat st;
    CHECK(stat(TMP_ROOT "/switch/kaiplayer/config/later.txt", &st) != 0);
    CHECK(appRootMigrate(now, now) == 0);
    CHECK(appRootMigrate(TMP_ROOT "/nowhere", TMP_ROOT "/switch/other/config") == 0);
}
