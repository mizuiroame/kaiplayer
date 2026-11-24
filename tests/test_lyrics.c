// lyrics.c: reading LRC files and finding the line being sung.
#include <string.h>

#include "check.h"
#include "lyrics.h"

static const char LRC[] =
    "\xEF\xBB\xBF[ti:Made-up Song]\r\n"
    "[ar:Nobody]\r\n"
    "[00:05.20]First line\r\n"
    "[00:12.5]  Second <00:13.00>line  \r\n"
    "[00:20][01:10.000]Chorus\r\n"
    "[00:30:50]Colon fraction\n"
    "[00:40.00]\n"
    "not timed\n"
    "[xx:yy]bad time\n"
    "[00:50.00]Last";

static void testParse(void)
{
    Lyrics l;
    CHECK(lyricsParse(LRC, strlen(LRC), &l));
    CHECK(l.count == 7);
    if (l.count == 7) {
        CHECK(l.lines[0].ms == 5200 && !strcmp(l.lines[0].text, "First line"));
        CHECK(l.lines[1].ms == 12500 && !strcmp(l.lines[1].text, "Second line"));
        CHECK(l.lines[2].ms == 20000 && !strcmp(l.lines[2].text, "Chorus"));
        CHECK(l.lines[3].ms == 30500 && !strcmp(l.lines[3].text, "Colon fraction"));
        CHECK(l.lines[4].ms == 40000 && !l.lines[4].text[0]);  // A pause.
        CHECK(l.lines[5].ms == 50000 && !strcmp(l.lines[5].text, "Last"));
        CHECK(l.lines[6].ms == 70000 && !strcmp(l.lines[6].text, "Chorus"));  // Sorted by time.
    }
    CHECK(lyricsLineAt(&l, 0) == -1 && lyricsLineAt(&l, 5.19) == -1);
    CHECK(lyricsLineAt(&l, 5.2) == 0 && lyricsLineAt(&l, 12.49) == 0 && lyricsLineAt(&l, 12.5) == 1);
    CHECK(lyricsLineAt(&l, 69) == 5 && lyricsLineAt(&l, 500) == 6);
    CHECK(lyricsLineAt(&l, -1) == -1 && lyricsLineAt(NULL, 3) == -1);
    lyricsFree(&l);
    CHECK(l.lines == NULL && l.count == 0);
}

static void testOffsetAndBad(void)
{
    Lyrics l;
    const char *shift = "[offset:+500]\n[00:01.00]a\n[00:00.20]b\n";
    CHECK(lyricsParse(shift, strlen(shift), &l) && l.count == 2);
    if (l.count == 2)
        CHECK(l.lines[0].ms == 0 && !strcmp(l.lines[0].text, "b") && l.lines[1].ms == 500);
    lyricsFree(&l);
    const char *later = "[offset:-250]\n[00:01.00]a\n";
    CHECK(lyricsParse(later, strlen(later), &l) && l.lines[0].ms == 1250);
    lyricsFree(&l);

    CHECK(!lyricsParse("", 0, &l) && !lyricsParse(NULL, 5, &l));
    CHECK(!lyricsParse("\n\n  \n[ar:Only tags]\n", 20, &l) && !l.lines && !l.count);
    const char *unclosed = "[00:01.00 never closed\n[00:99.00]bad seconds\n[1:02.3456]too fine\n";
    CHECK(lyricsParse(unclosed, strlen(unclosed), &l) && !l.synced && l.count == 3);  // Broken times: plain text.
    lyricsFree(&l);
    // A line with more times than kept, and no text after its times.
    char many[600] = "";
    for (int i = 0; i < 40; i++) {
        char stamp[16];
        snprintf(stamp, sizeof(stamp), "[00:%02d]", i);
        strcat(many, stamp);
    }
    CHECK(lyricsParse(many, strlen(many), &l) && l.count == 32 && !l.lines[0].text[0]);
    lyricsFree(&l);
}

// Lyrics without times: every line in order, tags out, blank lines only
// between verses; the text moves evenly through the song.
static void testPlain(void)
{
    Lyrics l;
    const char *plain = "[ti:Song]\r\n\r\nFirst line\r\n  second  \r\n\r\nNew verse\r\n\r\n\r\n";
    CHECK(lyricsParse(plain, strlen(plain), &l) && !l.synced && l.count == 4);
    if (l.count == 4) {
        CHECK(!strcmp(l.lines[0].text, "First line") && !strcmp(l.lines[1].text, "second"));
        CHECK(!l.lines[2].text[0] && !strcmp(l.lines[3].text, "New verse"));
    }
    CHECK(lyricsLineAt(&l, 30) == -1);  // No line is "being sung".
    CHECK(lyricsScrollAt(&l, 0, 200) == 0 && lyricsScrollAt(&l, 100, 200) == 1.5);
    CHECK(lyricsScrollAt(&l, 500, 200) == 3 && lyricsScrollAt(&l, 50, 0) == 0);
    lyricsFree(&l);

    const char *synced = "[00:01.00]a\nno time here\n";
    CHECK(lyricsParse(synced, strlen(synced), &l) && l.synced && l.count == 1);
    lyricsFree(&l);
}

static void testPaths(void)
{
    char out[64];
    CHECK(lyricsPathFor("sdmc:/Music/Song.flac", out, sizeof(out)) && !strcmp(out, "sdmc:/Music/Song.lrc"));
    CHECK(lyricsPathFor("dav:/A.B/Track 1.mp3", out, sizeof(out)) && !strcmp(out, "dav:/A.B/Track 1.lrc"));
    CHECK(lyricsPathFor("sdmc:/Music.dir/noext", out, sizeof(out)) && !strcmp(out, "sdmc:/Music.dir/noext.lrc"));
    CHECK(lyricsPathFor("sdmc:/x/.hidden", out, sizeof(out)) && !strcmp(out, "sdmc:/x/.hidden.lrc"));
    CHECK(!lyricsPathFor("sdmc:/a/very/long/path/that/does/not/fit/in/the/buffer/song.flac", out, 20));
}

void testLyrics(void)
{
    testParse();
    testOffsetAndBad();
    testPlain();
    testPaths();
}
