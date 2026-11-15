// Tests for paths, track lists, playlists, resume, the keyboard and menus.
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "check.h"
#include "keyboard.h"
#include "menu.h"
#include "playlist.h"
#include "resume.h"
#include "tracklist.h"
#include "util.h"

static bool resolved(const char *base, const char *ref, const char *expected)
{
    char out[UTIL_PATH_MAX];
    return utilResolvePath(base, ref, out, sizeof(out)) && strcmp(out, expected) == 0;
}

static bool relative(const char *from, const char *to, const char *expected)
{
    char out[UTIL_PATH_MAX];
    return utilRelativePath(from, to, out, sizeof(out)) && strcmp(out, expected) == 0;
}

static void testPaths(void)
{
    char p[64] = "sdmc:/a/./b//../c/";
    CHECK(utilNormalizePath(p) && strcmp(p, "sdmc:/a/c") == 0);
    strcpy(p, "sdmc:/..");
    CHECK(!utilNormalizePath(p));

    CHECK(resolved("sdmc:/playlists", "../Music/A/01.flac", "sdmc:/Music/A/01.flac"));
    CHECK(resolved("sdmc:/playlists", "..\\Music\\A\\01.flac", "sdmc:/Music/A/01.flac"));
    CHECK(resolved("sdmc:/playlists", "/Music/x.mp3", "sdmc:/Music/x.mp3"));
    CHECK(resolved("sdmc:/playlists", "sdmc:/Music/x.mp3", "sdmc:/Music/x.mp3"));
    CHECK(resolved("sdmc:/playlists", "song.mp3", "sdmc:/playlists/song.mp3"));
    CHECK(!resolved("sdmc:/playlists", "../../x.mp3", ""));

    CHECK(relative("sdmc:/playlists", "sdmc:/Music/A/01.flac", "../Music/A/01.flac"));
    CHECK(relative("sdmc:/Music", "sdmc:/Music/A/01.flac", "A/01.flac"));
    CHECK(relative("sdmc:/", "sdmc:/Music/x.mp3", "Music/x.mp3"));
    CHECK(relative("sdmc:/a/b", "sdmc:/c/d.mp3", "../../c/d.mp3"));
    CHECK(relative("sdmc:/playlists", "usb:/x.mp3", "usb:/x.mp3"));
}

static void testTrackList(void)
{
    TrackList l = {0};
    CHECK(trackListAdd(&l, "a") && trackListAdd(&l, "b") && trackListAdd(&l, "c") && trackListAdd(&l, "d"));
    trackListMove(&l, 0, 2);  // b c a d
    CHECK(strcmp(l.paths[0], "b") == 0 && strcmp(l.paths[2], "a") == 0);
    trackListMove(&l, 3, 0);  // d b c a
    CHECK(strcmp(l.paths[0], "d") == 0 && strcmp(l.paths[3], "a") == 0);
    trackListMove(&l, 9, 0);  // Out of range: nothing.
    trackListRemove(&l, 1);   // d c a
    CHECK(l.count == 3 && strcmp(l.paths[1], "c") == 0);
    trackListRemove(&l, 7);
    CHECK(l.count == 3);
    TrackList copy;
    CHECK(trackListCopy(&copy, &l) && copy.count == 3 && copy.paths[0] != l.paths[0]);
    CHECK(trackListFind(&copy, "a") == 2);
    trackListFree(&copy);
    trackListFree(&l);
}

static void testParseFormat(void)
{
    const char *text = "\xEF\xBB\xBF#EXTM3U\r\n#EXTINF:123,Artist - Title\r\n"
                       "../Music/A/01 Intro.flac\r\n\r\n  /Music/B/02.mp3  \nsdmc:/x.ogg";
    TrackList l;
    CHECK(playlistParse(text, strlen(text), "sdmc:/playlists", &l));
    CHECK(l.count == 3);
    if (l.count == 3) {
        CHECK(strcmp(l.paths[0], "sdmc:/Music/A/01 Intro.flac") == 0);
        CHECK(strcmp(l.paths[1], "sdmc:/Music/B/02.mp3") == 0);
        CHECK(strcmp(l.paths[2], "sdmc:/x.ogg") == 0);
    }
    char *out = playlistFormat(&l, "sdmc:/playlists");
    CHECK(out && strcmp(out, "#EXTM3U\n../Music/A/01 Intro.flac\n../Music/B/02.mp3\n../x.ogg\n") == 0);
    free(out);
    trackListFree(&l);
}

static void testPlaylistFiles(void)
{
    const char *root = TMP_ROOT "/playlists";
    playlistSetRoot(root);
    CHECK(playlistNameValid("Road Trip 2026"));
    CHECK(playlistNameValid("Rock & Roll (live)!"));
    CHECK(!playlistNameValid(""));
    CHECK(!playlistNameValid(".hidden"));
    CHECK(!playlistNameValid("a/b"));
    CHECK(!playlistNameValid("日本"));
    CHECK(!playlistNameValid("trailing "));

    CHECK(playlistCreate("Mix"));
    CHECK(!playlistCreate("Mix"));  // Taken.
    CHECK(playlistCreate("Another"));
    PlaylistIndex index;
    CHECK(playlistIndexLoad(&index) && index.count == 2);
    if (index.count == 2)
        CHECK(strcmp(index.names[0], "Another") == 0 && strcmp(index.names[1], "Mix") == 0);
    playlistIndexFree(&index);

    TrackList add = {0};
    trackListAdd(&add, TMP_ROOT "/lib/z.mp3");
    trackListAdd(&add, TMP_ROOT "/lib/C.flac");
    CHECK(playlistAddTracks("Mix", &add) == 2);
    CHECK(playlistAddTracks("Mix", &add) == 0);  // Already there.
    trackListFree(&add);

    char path[UTIL_PATH_MAX];
    TrackList loaded;
    CHECK(playlistPathFor("Mix", path, sizeof(path)) && playlistLoad(path, &loaded));
    CHECK(loaded.count == 2 && strcmp(loaded.paths[0], TMP_ROOT "/lib/z.mp3") == 0);
    trackListFree(&loaded);

    CHECK(!playlistRename("Mix", "Another"));  // Taken.
    CHECK(playlistRename("Mix", "Favorites") && playlistExists("Favorites") && !playlistExists("Mix"));
    CHECK(playlistDelete("Favorites") && !playlistExists("Favorites"));
    CHECK(playlistAddTracks("Missing", &loaded) == -1);
}

static void testResume(void)
{
    resumeSetDir(TMP_ROOT "/config");
    TrackList q = {0}, back;
    size_t track;
    double position;
    CHECK(!resumeLoad(&back, &track, &position));  // Nothing saved yet.
    trackListAdd(&q, TMP_ROOT "/lib/z.mp3");
    trackListAdd(&q, TMP_ROOT "/lib/C.flac");
    CHECK(resumeSave(&q, 1, 83.4));
    CHECK(resumeLoad(&back, &track, &position));
    CHECK(back.count == 2 && track == 1 && position > 83.3 && position < 83.5);
    trackListFree(&back);
    CHECK(resumeSave(&q, 9, -5.0));  // Out of range values are corrected on load.
    CHECK(resumeLoad(&back, &track, &position) && track == 0 && position == 0.0);
    trackListFree(&back);
    resumeClear();
    CHECK(!resumeLoad(&back, &track, &position));
    trackListFree(&q);
}

static void testKeyboard(void)
{
    Keyboard k;
    keyboardInit(&k, "Ab");
    CHECK(strcmp(k.text, "Ab") == 0 && k.row == 1 && k.col == 0);
    CHECK(keyboardPress(&k) == KEYBOARD_EDITING && strcmp(k.text, "Abq") == 0);
    keyboardToggleShift(&k);
    keyboardMove(&k, 1, 0);
    keyboardPress(&k);
    CHECK(strcmp(k.text, "AbqW") == 0);
    keyboardBackspace(&k);
    CHECK(strcmp(k.text, "Abq") == 0);

    keyboardMove(&k, 0, 3);  // Row 1 -> the action row.
    CHECK(k.row == KEYBOARD_CHAR_ROWS && k.col == 0);
    keyboardPress(&k);       // Space.
    CHECK(strcmp(k.text, "Abq ") == 0);
    keyboardMove(&k, -1, 0); // Wraps to Done.
    CHECK(k.col == KEY_ACTION_DONE && keyboardPress(&k) == KEYBOARD_DONE);
    keyboardMove(&k, 0, 1);  // Wraps to the digits row.
    CHECK(k.row == 0 && k.col < KEYBOARD_COLS);

    keyboardInit(&k, NULL);
    for (int i = 0; i < KEYBOARD_MAX + 5; i++)
        keyboardType(&k, 'x');
    CHECK(strlen(k.text) == KEYBOARD_MAX);
    for (int r = 0; r < KEYBOARD_CHAR_ROWS; r++) {
        for (int c = 0; c < KEYBOARD_COLS; c++) {
            char name[2] = {keyboardCharAt(&k, r, c), 0};
            CHECK(playlistNameValid(name) || name[0] == '.');  // Every key is safe in a name.
        }
    }
}

static void testMenu(void)
{
    Menu m;
    menuOpen(&m, "Title");
    CHECK(menuSelected(&m) == NULL);
    CHECK(menuAdd(&m, "One", 1, 0) && menuAdd(&m, "Two", 2, 5));
    menuMove(&m, 5);
    CHECK(menuSelected(&m)->action == 2 && menuSelected(&m)->arg == 5);
    menuMove(&m, -9);
    CHECK(menuSelected(&m)->action == 1);
    for (int i = 0; i < MENU_MAX_ITEMS; i++)
        menuAdd(&m, "x", 0, 0);
    CHECK(m.count == MENU_MAX_ITEMS && !menuAdd(&m, "full", 0, 0));
    menuClose(&m);
    CHECK(menuSelected(&m) == NULL);
}

void testPlaylists(void)
{
    mkdir(TMP_ROOT "/config", 0777);
    testPaths();
    testTrackList();
    testParseFormat();
    testPlaylistFiles();
    testResume();
    testKeyboard();
    testMenu();
}
