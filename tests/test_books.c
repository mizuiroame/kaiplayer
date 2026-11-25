// Audiobooks: the reading speed (tempo.c), chapters (bookchap.c, ffaudio.c),
// books from files (books.c), where each was left (bookstore.c) and a
// Navidrome server's (booksub.c).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bookchap.h"
#include "books.h"
#include "bookstore.h"
#include "booksub.h"
#include "check.h"
#include "ffaudio.h"
#include "kfile.h"
#include "tempo.h"

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)
#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) < (eps))

// --- Tempo ---

#define RATE 48000
#define TONE_HZ 220.0

typedef struct {
    uint64_t at, total;
} ToneSource;

static uint64_t readTone(void *ctx, float *out, uint64_t frames)
{
    ToneSource *s = ctx;
    uint64_t n = 0;
    for (; n < frames && s->at < s->total; n++, s->at++) {
        float v = 0.5f * (float)sin(2.0 * M_PI * TONE_HZ * (double)s->at / RATE);
        out[n * 2] = out[n * 2 + 1] = v;
    }
    return n;
}

// Zero crossings going up, per second, over the middle of the sound: its pitch.
static double pitchOf(const float *pcm, size_t frames)
{
    size_t from = frames / 4, to = frames * 3 / 4, ups = 0;
    for (size_t i = from + 1; i < to; i++)
        if (pcm[(i - 1) * 2] < 0.0f && pcm[i * 2] >= 0.0f)
            ups++;
    return (double)ups * RATE / (double)(to - from);
}

static void testTempoAt(int permille)
{
    static Tempo t;
    tempoInit(&t, permille);
    ToneSource src = {0, RATE * 2};
    size_t cap = RATE * 6;
    float *out = malloc(cap * 2 * sizeof(float));
    size_t got = 0;
    for (;;) {
        uint64_t n = tempoRun(&t, readTone, &src, out + got * 2, 1000);
        got += (size_t)n;
        if (n < 1000 || got + 1000 > cap)
            break;
    }
    double expected = 2.0 * RATE * TEMPO_PERMILLE / permille;
    CHECK(fabs((double)got - expected) < TEMPO_WINDOW * 2);  // The length changes with the speed,
    CHECK_NEAR(pitchOf(out, got), TONE_HZ, 3.0);              // the pitch doesn't.
    float peak = 0.0f;
    for (size_t i = 0; i < got * 2; i++)
        peak = fabsf(out[i]) > peak ? fabsf(out[i]) : peak;
    CHECK(peak > 0.4f && peak < 0.62f);  // No doubled or lost level where windows meet.
    CHECK(llabs(tempoPosition(&t) - (int64_t)src.total) < TEMPO_WINDOW * 2);
    free(out);
}

static void testTempo(void)
{
    testTempoAt(750);
    testTempoAt(1500);
    testTempoAt(2000);
    char s[16];
    tempoFormatSpeed(s, sizeof(s), 1000);
    CHECK_STR(s, "1x");
    tempoFormatSpeed(s, sizeof(s), 1250);
    CHECK_STR(s, "1.25x");
    tempoFormatSpeed(s, sizeof(s), 1500);
    CHECK_STR(s, "1.5x");
    tempoFormatSpeed(s, sizeof(s), 750);
    CHECK_STR(s, "0.75x");
    CHECK(tempoSpeedPermille(TEMPO_SPEED_NORMAL) == 1000);
    CHECK(tempoSpeedPermille(99) == 1000);
    CHECK(tempoSpeedIndex(1490) == 3 && tempoSpeedIndex(100) == 0 && tempoSpeedIndex(9000) == TEMPO_SPEED_COUNT - 1);
    CHECK(tempoSpeedStep(0, -1) == 0 && tempoSpeedStep(TEMPO_SPEED_COUNT - 1, 1) == TEMPO_SPEED_COUNT - 1);
    CHECK(tempoSpeedStep(1, 2) == 3);

    // An empty source ends at once; a reset starts the position over.
    static Tempo t;
    tempoInit(&t, 1250);
    ToneSource none = {0, 0};
    float buf[64];
    CHECK(tempoRun(&t, readTone, &none, buf, 32) == 0);
    tempoReset(&t);
    CHECK(tempoPosition(&t) == 0);
}

// --- Chapters ---

static uint8_t *readFixture(const char *name, size_t *size)
{
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    *size = data && fread(data, 1, (size_t)n, f) == (size_t)n ? (size_t)n : 0;
    fclose(f);
    return data;
}

static void testChapters(void)
{
    BookChapters c = {0};
    size_t size;
    uint8_t *mp3 = readFixture("tone_book_chapters.mp3", &size);
    CHECK(mp3 && bookChaptersParseId3(mp3, size, &c));
    bookChaptersPrepare(&c);
    CHECK(c.count == 3);
    if (c.count == 3) {
        CHECK_STR(c.items[0].title, "Opening");
        CHECK_STR(c.items[1].title, "The Middle");
        CHECK_NEAR(c.items[2].start, 2.0, 0.001);
    }
    // Cut anywhere, it reads what it can and never past the end.
    for (size_t cut = 0; mp3 && cut < 0x140; cut++) {
        BookChapters part = {0};
        bookChaptersParseId3(mp3, cut, &part);
        CHECK(part.count <= 3);
        bookChaptersFree(&part);
    }
    free(mp3);
    bookChaptersFree(&c);

    static const char CUE[] = "\xEF\xBB\xBFPERFORMER \"Someone\"\r\nFILE \"book.mp3\" MP3\r\n"
                              "  TRACK 01 AUDIO\r\n    TITLE \"Prologue\"\r\n    INDEX 01 00:00:00\r\n"
                              "  TRACK 02 AUDIO\r\n    TITLE \"Chapter One\"\r\n    INDEX 00 01:59:00\r\n    INDEX 01 02:00:37\r\n"
                              "  TRACK 03 AUDIO\r\n    INDEX 01 75:00:00\r\n";
    CHECK(bookChaptersParseCue(CUE, sizeof(CUE) - 1, &c));
    bookChaptersPrepare(&c);
    CHECK(c.count == 3);
    if (c.count == 3) {
        CHECK_STR(c.items[0].title, "Prologue");
        CHECK_NEAR(c.items[1].start, 120.0 + 37.0 / 75.0, 0.001);
        CHECK_STR(c.items[2].title, "Chapter 3");  // Named when it has no title.
        CHECK_NEAR(c.items[2].start, 4500.0, 0.001);
        CHECK(bookChapterAt(&c, 0.0) == 0 && bookChapterAt(&c, 130.0) == 1 && bookChapterAt(&c, 99999.0) == 2);
    }
    bookChaptersFree(&c);
    static const char TWO_FILES[] = "FILE \"a.mp3\" MP3\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nFILE \"b.mp3\" MP3\n"
                                    "TRACK 02 AUDIO\nINDEX 01 00:00:00\n";
    CHECK(!bookChaptersParseCue(TWO_FILES, sizeof(TWO_FILES) - 1, &c) && c.count == 0);
    CHECK(!bookChaptersParseCue("REM nothing\n", 12, &c));

    // Out of order and the same start twice: sorted, one kept with the title.
    bookChaptersAdd(&c, 30.0, "B");
    bookChaptersAdd(&c, 0.0, NULL);
    bookChaptersAdd(&c, 0.0, "A");
    bookChaptersPrepare(&c);
    CHECK(c.count == 2 && strcmp(c.items[0].title, "A") == 0 && c.items[1].start == 30.0);
    bookChaptersFree(&c);

    // An M4B's chapter track, through FFmpeg.
    KFile *f = kfileOpen(FIXTURES "/tone_book.m4b", KFILE_PEEK);
    CHECK(f && ffaudioReadChapters(f, &c));
    bookChaptersPrepare(&c);
    CHECK(c.count == 3);
    if (c.count == 3) {
        CHECK_STR(c.items[0].title, "Opening");
        CHECK_STR(c.items[2].title, "Ending");
        CHECK_NEAR(c.items[1].start, 1.0, 0.05);
    }
    kfileClose(f);
    bookChaptersFree(&c);
    CHECK(ffaudioIsFile("x/Book.M4B"));
    f = kfileOpen(FIXTURES "/tone.m4a", KFILE_PEEK);  // A song without chapters.
    CHECK(f && !ffaudioReadChapters(f, &c) && c.count == 0);
    kfileClose(f);
}

// --- Books ---

static void testBookNames(void)
{
    CHECK(booksIsBookName("Audiobooks") && booksIsBookName("audio books") && booksIsBookName("Hörbücher"));
    CHECK(booksIsBookName("AUDIOLIBROS") && booksIsBookName("Livres audio") && booksIsBookName("オーディオブック"));
    CHECK(!booksIsBookName("Books") && !booksIsBookName("Music") && !booksIsBookName(""));
    CHECK(booksIsDiscFolder("CD 1") && booksIsDiscFolder("Disc02") && booksIsDiscFolder("part_3"));
    CHECK(!booksIsDiscFolder("CD") && !booksIsDiscFolder("Cdx1") && !booksIsDiscFolder("Chapter 1"));
    CHECK(booksIsBookPath("sdmc:/Music/Audiobooks/Author/Book/01.mp3"));
    CHECK(booksIsBookPath("dav:/Stuff/novel.m4b"));
    CHECK(!booksIsBookPath("sdmc:/Music/Rock/song.mp3"));
    CHECK(booksCompareNatural("Chapter 2", "Chapter 10") < 0);
    CHECK(booksCompareNatural("chapter 02", "Chapter 2") == 0);
    CHECK(booksCompareNatural("b", "A") > 0 && booksCompareNatural("a", "ab") < 0);
}

static void testGrouping(void)
{
    static const char *const PATHS[] = {
        "sdmc:/audiobooks/Ada/Long Book/CD 2/01.mp3",
        "sdmc:/audiobooks/Ada/Long Book/CD 1/02.mp3",
        "sdmc:/audiobooks/Ada/Long Book/CD 1/01.mp3",
        "sdmc:/audiobooks/Tiny.m4b",
        "sdmc:/audiobooks/Loose.mp3",
        "sdmc:/audiobooks/Short Book/Chapter 10.mp3",
        "sdmc:/audiobooks/Short Book/Chapter 2.mp3",
        "sdmc:/audiobooks/Short Book/Chapter 2.mp3",
        "sdmc:/audiobooks/Short Book/Extra.m4b",
    };
    static const long SIZES[] = {100, 200, 300, 400, 500, 600, 700, 700, 800};
    BookList l = {0};
    CHECK(booksGroup(&l, PATHS, SIZES, sizeof(PATHS) / sizeof(PATHS[0])));
    CHECK(l.count == 5);
    booksSortByTitle(&l);
    const Book *lng = booksFind(&l, "sdmc:/audiobooks/Ada/Long Book");
    CHECK(lng && lng->part_count == 3);
    if (lng && lng->part_count == 3) {
        CHECK_STR(lng->title, "Long Book");
        CHECK(strstr(lng->parts[0].path, "CD 1/01.mp3") && strstr(lng->parts[2].path, "CD 2/01.mp3"));
        CHECK(lng->parts[0].size == 300);
    }
    const Book *shrt = booksFind(&l, "sdmc:/audiobooks/Short Book");
    CHECK(shrt && shrt->part_count == 2 && strstr(shrt->parts[0].path, "Chapter 2"));  // Once, in number order.
    const Book *tiny = booksFind(&l, "sdmc:/audiobooks/Tiny.m4b");
    CHECK(tiny && strcmp(tiny->title, "Tiny") == 0);
    CHECK(booksFind(&l, "sdmc:/audiobooks/Loose.mp3") && booksFind(&l, "sdmc:/audiobooks/Short Book/Extra.m4b"));
    CHECK_STR(l.items[0].title, "Extra");  // By title.

    // Lengths and chapters: one per file, then where a time is.
    Book b;
    CHECK(bookCopy(&b, shrt));
    b.parts[0].duration = 100.0;
    b.parts[1].duration = 50.0;
    static const char *const TITLES[] = {"The Start", ""};
    bookFinishProbe(&b, TITLES);
    CHECK(b.probed && b.duration == 150.0 && b.chapters.count == 2);
    CHECK_STR(b.chapters.items[0].title, "The Start");
    CHECK_STR(b.chapters.items[1].title, "Chapter 10");  // The file's name.
    CHECK(bookChaptersAdd(&b.chapters, 0, NULL));        // (prepared below again, harmless)
    CHECK(bookPartStart(&b, 1) == 100.0 && bookTimeAt(&b, 1, 20.0) == 120.0);
    size_t part;
    double pos;
    bookLocate(&b, 120.0, &part, &pos);
    CHECK(part == 1 && pos == 20.0);
    bookLocate(&b, 500.0, &part, &pos);
    CHECK(part == 1 && pos == 50.0);
    bookLocate(&b, -3.0, &part, &pos);
    CHECK(part == 0 && pos == 0.0);
    bookChaptersPrepare(&b.chapters);
    CHECK(bookChapterIndex(&b, 99.0) == 0 && bookChapterIndex(&b, 100.0) == 1 && bookChapterEnd(&b, 0) == 100.0);
    CHECK(bookChapterEnd(&b, 1) == 150.0);

    // The cache: written and read back the same; a changed file is noticed.
    BookList one = {0};
    Book copy;
    CHECK(bookCopy(&copy, &b) && bookListTake(&one, &copy));
    snprintf(one.items[0].author, sizeof(one.items[0].author), "Ada\tExample");
    FILE *f = tmpfile();
    CHECK(f && booksSave(&one, f));
    rewind(f);
    BookList back = {0};
    CHECK(booksLoad(&back, f));
    fclose(f);
    CHECK(back.count == 1);
    if (back.count == 1) {
        const Book *r = &back.items[0];
        CHECK(r->probed && r->part_count == 2 && r->chapters.count == 2 && r->duration == 150.0);
        CHECK_STR(r->author, "Ada Example");
        CHECK_STR(r->chapters.items[0].title, "The Start");
        CHECK(bookSameParts(r, &b));
        CHECK(!bookSameParts(r, lng));
    }
    bookListFree(&back);
    bookListFree(&one);
    bookFree(&b);
    bookListFree(&l);

    f = tmpfile();  // Damaged lines are left out.
    fputs("part\t1\t2\tlost\nbook\t1\t0\t\nbook\t1\t0\tk\npart\tx\t-5\tsdmc:/a.mp3\nchapter\tz\nend\nend\n", f);
    rewind(f);
    CHECK(booksLoad(&back, f) && back.count == 1 && back.items[0].part_count == 1 && back.items[0].duration == 0.0);
    fclose(f);
    bookListFree(&back);
}

static void testStore(void)
{
    BookStore s = {0};
    CHECK(bookStoreSet(&s, "sdmc:/audiobooks/A", 2, 61.5, 1000, false));
    CHECK(bookStoreSet(&s, "navidrome:al-9", 0, 5.0, 2000, true));
    CHECK(bookStoreSet(&s, "sdmc:/audiobooks/A", 3, 1.25, 3000, false));  // Replaces.
    CHECK(!bookStoreSet(&s, "bad\nkey", 0, 0, 0, false) && !bookStoreSet(&s, "", 0, 0, 0, false));
    CHECK(bookStoreSetActive(&s, "navidrome:al-9"));
    s.speed = 1250;
    FILE *f = tmpfile();
    CHECK(bookStoreSave(&s, f));
    fputs("mark\tbroken\nmark\t1\t2\t3\n", f);
    rewind(f);
    BookStore r = {0};
    CHECK(bookStoreLoad(&r, f));
    fclose(f);
    CHECK(r.count == 2 && r.speed == 1250 && r.active && strcmp(r.active, "navidrome:al-9") == 0);
    const BookMark *m = bookStoreFind(&r, "sdmc:/audiobooks/A");
    CHECK(m && m->part == 3 && fabs(m->position - 1.25) < 0.01 && m->updated == 3000 && !m->finished);
    m = bookStoreFind(&r, "navidrome:al-9");
    CHECK(m && m->finished);
    bookStoreForget(&r, "navidrome:al-9");
    CHECK(!bookStoreFind(&r, "navidrome:al-9") && r.count == 1);
    CHECK(bookStoreSetActive(&r, NULL) && !r.active);
    bookStoreFree(&r);
    bookStoreFree(&s);
}

static JsonDoc *parse(const char *text, const JsonValue **body)
{
    JsonDoc *d = jsonParse(text, strlen(text));
    const JsonValue *root = d ? jsonRoot(d) : NULL;
    int code;
    char message[64];
    if (!root || !subAnswer(root, body, &code, message, sizeof(message)))
        *body = NULL;
    return d;
}

static void testServerBooks(void)
{
    const JsonValue *body;
    JsonDoc *d = parse("{\"subsonic-response\":{\"status\":\"ok\",\"genres\":{\"genre\":[{\"value\":\"Rock\"},"
                       "{\"value\":\"Audiobook\",\"albumCount\":2},{\"value\":\"Hörbuch\"}]}}}", &body);
    BookSubList lists[4];
    CHECK(body && bookSubGenreLists(body, lists, 4) == 2);
    CHECK_STR(lists[0].params, "type=byGenre&genre=Audiobook");
    CHECK_STR(lists[1].params, "type=byGenre&genre=H%C3%B6rbuch");
    jsonFree(d);
    d = parse("{\"subsonic-response\":{\"status\":\"ok\",\"musicFolders\":{\"musicFolder\":[{\"id\":1,\"name\":\"Music\"},"
              "{\"id\":2,\"name\":\"Audiobooks\"}]}}}", &body);
    CHECK(body && bookSubFolderLists(body, lists, 4) == 1);
    CHECK_STR(lists[0].params, "type=alphabeticalByName&musicFolderId=2");
    jsonFree(d);

    d = parse("{\"subsonic-response\":{\"status\":\"ok\",\"albumList2\":{\"album\":[{\"id\":\"a1\"},{\"id\":\"a2\"},"
              "{\"id\":\"a1\"}]}}}", &body);
    char ids[4][SUB_ID_MAX];
    size_t page;
    CHECK(body && bookSubAlbumIds(body, ids, 0, 4, &page) == 2 && page == 3);
    CHECK(bookSubAlbumIds(body, ids, 2, 4, &page) == 2);  // Known already.
    jsonFree(d);

    d = parse("{\"subsonic-response\":{\"status\":\"ok\",\"album\":{\"id\":\"al-9\",\"name\":\"A Made-up Book\","
              "\"artist\":\"Ada Example\",\"coverArt\":\"al-9\",\"song\":["
              "{\"id\":\"s2\",\"title\":\"Part Two\",\"track\":2,\"duration\":50,\"suffix\":\"mp3\",\"size\":800},"
              "{\"id\":\"s1\",\"title\":\"Part One\",\"track\":1,\"duration\":100,\"suffix\":\"mp3\"},"
              "{\"id\":\"s3\",\"title\":\"Video\",\"isVideo\":true,\"suffix\":\"mp4\"}]}}}", &body);
    Book b;
    CHECK(body && bookSubAlbum(body, &b));
    CHECK_STR(b.key, "navidrome:al-9");
    CHECK_STR(b.title, "A Made-up Book");
    CHECK_STR(b.author, "Ada Example");
    CHECK(b.server && b.probed && b.part_count == 2 && b.duration == 150.0 && b.chapters.count == 2);
    if (b.part_count == 2) {
        CHECK_STR(b.parts[0].path, "sub:/al-9/s1.mp3");
        CHECK_STR(b.chapters.items[1].title, "Part Two");
        CHECK(b.chapters.items[1].start == 100.0);
    }
    char song[SUB_ID_MAX];
    CHECK(bookSubSongId("sub:/al-9/s2.mp3", song, sizeof(song)) && strcmp(song, "s2") == 0);
    jsonFree(d);

    BookList l = {0};
    CHECK(bookListTake(&l, &b));
    d = parse("{\"subsonic-response\":{\"status\":\"ok\",\"bookmarks\":{\"bookmark\":["
              "{\"entry\":{\"id\":\"s1\"},\"position\":90000,\"changed\":\"2026-11-20T10:00:00Z\"},"
              "{\"entry\":{\"id\":\"s2\"},\"position\":12500,\"changed\":\"2026-11-21T10:00:00.25Z\"},"
              "{\"entry\":{\"id\":\"other\"},\"position\":1}]}}}", &body);
    CHECK(body);
    bookSubApplyBookmarks(body, &l);
    CHECK(l.items[0].mark_part == 1 && fabs(l.items[0].mark_position - 12.5) < 0.001);
    CHECK(l.items[0].mark_time == bookSubTime("2026-11-21T10:00:00Z"));
    jsonFree(d);
    bookListFree(&l);

    d = parse("{\"subsonic-response\":{\"status\":\"ok\",\"album\":{\"id\":\"x\",\"song\":["
              "{\"id\":\"one\",\"duration\":3600,\"suffix\":\"m4b\"}]}}}", &body);
    CHECK(body && bookSubAlbum(body, &b) && !b.probed && b.duration == 3600.0);  // Chapters from the file.
    bookFree(&b);
    jsonFree(d);

    CHECK(bookSubTime("1970-01-01T00:00:00Z") == 0);
    CHECK(bookSubTime("2000-03-01T00:00:00Z") == 951868800LL);
    CHECK(bookSubTime("2000-03-01T01:30:00+01:30") == 951868800LL);
    CHECK(bookSubTime("yesterday") == 0 && bookSubTime(NULL) == 0);
}

void testBooks(void)
{
    testTempo();
    testChapters();
    testBookNames();
    testGrouping();
    testStore();
    testServerBooks();
}
