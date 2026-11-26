// The Internet Archive (stream.h, streammeta.h): locations, licenses, the requests' URLs, and its answers,
// from made-up fixtures (never the real Archive).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "json.h"
#include "playlist.h"
#include "stream.h"
#include "streammeta.h"

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

static char *readFixture(const char *name, size_t *size)
{
    char path[256];
    snprintf(path, sizeof(path), FIXTURES "/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data)
        data[n] = '\0';
    *size = (size_t)n;
    return data;
}

// The fixture's answer to q into list; whether it parsed, with why.
static bool parseFixture(const char *name, const StreamQuery *q, StreamList *list, char *why, size_t why_size)
{
    size_t size = 0;
    char *text = readFixture(name, &size);
    CHECK(text != NULL);
    JsonDoc *doc = text ? jsonParse(text, size) : NULL;
    CHECK(doc != NULL);
    bool ok = doc && streamParseAnswer(q, jsonRoot(doc), list, why, why_size);
    jsonFree(doc);
    free(text);
    return ok;
}

static bool parseText(const char *text, const StreamQuery *q, StreamList *list, char *why, size_t why_size)
{
    JsonDoc *doc = jsonParse(text, strlen(text));
    CHECK(doc != NULL);
    bool ok = doc && streamParseAnswer(q, jsonRoot(doc), list, why, why_size);
    jsonFree(doc);
    return ok;
}

static void testLocations(void)
{
    char loc[UTIL_PATH_MAX], cover[UTIL_PATH_MAX], url[STREAM_URL_MAX];
    StreamTarget t;
    CHECK(streamSongLocation("ndb2006-01-14.made.up", "lma", "disc 2/song #1.mp3", loc, sizeof(loc)));
    CHECK_STR(loc, "ia:/ndb2006-01-14.made.up/lma/disc%202%2Fsong%20%231.mp3");
    CHECK(streamIsLocation(loc) && !streamIsLocation("sub:/x/1.mp3") && !streamIsLocation(NULL));
    CHECK(streamParseLocation(loc, &t) && !t.cover);
    CHECK_STR(t.identifier, "ndb2006-01-14.made.up");
    CHECK_STR(t.license, "lma");
    CHECK_STR(t.file, "disc 2/song #1.mp3");
    CHECK(streamFileUrl(&t, url, sizeof(url)));
    CHECK_STR(url, "https://archive.org/download/ndb2006-01-14.made.up/disc%202/song%20%231.mp3");
    CHECK(!streamSongLocation("bad id", "lma", "a.mp3", loc, sizeof(loc)));
    CHECK(!streamSongLocation("..", "lma", "a.mp3", loc, sizeof(loc)));
    CHECK(!streamSongLocation("ok", "lma", "notes.txt", loc, sizeof(loc)));
    CHECK(!streamSongLocation("ok", "LMA", "a.mp3", loc, sizeof(loc)));
    CHECK(!streamSongLocation("ok", "lma", "a\x01.mp3", loc, sizeof(loc)));
    CHECK(!streamSongLocation("ok", "lma", "a.mp3", loc, 12));  // Doesn't fit.
    CHECK(streamSongLocation("ok", "78rpm", "A.FLAC", loc, sizeof(loc)) && streamParseLocation(loc, &t));

    // Covers: a song's item's, and the key an item's row shows it by.
    CHECK(streamCoverLocation("ia:/ndb.x/lma/a.mp3", 256, cover, sizeof(cover)));
    CHECK_STR(cover, "ia:/ndb.x/cover/256.jpg");
    CHECK(streamParseLocation(cover, &t) && t.cover && t.px == 256);
    CHECK(streamCoverUrl(&t, url, sizeof(url)));
    CHECK_STR(url, "https://archive.org/services/img/ndb.x");
    CHECK(!streamFileUrl(&t, url, sizeof(url)));  // A cover isn't a song.
    CHECK(streamItemCoverLocation("ndb.x", cover, sizeof(cover)));
    CHECK_STR(cover, "ia:/ndb.x/cover/0.jpg");
    CHECK(streamCoverLocation(cover, 300, cover, sizeof(cover)));
    CHECK_STR(cover, "ia:/ndb.x/cover/300.jpg");
    CHECK(!streamItemCoverLocation("no/slash", cover, sizeof(cover)));
    CHECK(!streamCoverLocation("ia:/ndb.x/lma/a.mp3", 0, cover, sizeof(cover)));

    static const char *const BAD[] = {
        "ia:/", "ia://", "ia:/x/lma/", "ia:/x/lma/a%2.mp3", "ia:/x/lma/a%00.mp3", "ia:/x/lma/a.txt",
        "ia:/../lma/a.mp3", "ia:/x/lma/a/b.mp3", "ia:/x//a.mp3", "ia:/x/cover/99999.jpg", "ia:/x/cover/x.jpg",
        "ia:/x/cover/1.png", "ia:/x/LMA/a.mp3", "sdmc:/music/a.mp3", "jam:/1/by/1.mp3",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
        CHECK(!streamParseLocation(BAD[i], &t));

    // Playlists keep these locations as they are.
    TrackList list = {0};
    CHECK(trackListAdd(&list, "ia:/ndb.x/lma/disc%202%2Fsong%20%231.mp3"));
    CHECK(trackListAdd(&list, "ia:/madeup-netlabel-001/by-sa-4.0/01%20-%20Quiet%20Room.ogg"));
    char *text = playlistFormat(&list, "sdmc:/music/Playlists", NULL);
    TrackList back = {0};
    CHECK(text && playlistParse(text, strlen(text), "sdmc:/music/Playlists", &back) && back.count == 2);
    if (back.count == 2) {
        CHECK_STR(back.paths[0], list.paths[0]);
        CHECK_STR(back.paths[1], list.paths[1]);
    }
    free(text);
    trackListFree(&list);
    trackListFree(&back);
}

static void testLicenses(void)
{
    static const struct {
        const char *url, *code, *name, *link;
    } CASES[] = {
        {"http://creativecommons.org/licenses/by-nc-sa/3.0/", "by-nc-sa-3.0", "CC BY-NC-SA 3.0",
         "creativecommons.org/licenses/by-nc-sa/3.0"},
        {"https://creativecommons.org/licenses/by/4.0", "by-4.0", "CC BY 4.0", "creativecommons.org/licenses/by/4.0"},
        {"http://creativecommons.org/licenses/by-nd/", "by-nd", "CC BY-ND", "creativecommons.org/licenses/by-nd"},
        {"http://creativecommons.org/publicdomain/zero/1.0/", "cc0-1.0", "CC0 1.0",
         "creativecommons.org/publicdomain/zero/1.0"},
        {"http://creativecommons.org/publicdomain/mark/1.0/", "pdm-1.0", "Public domain",
         "creativecommons.org/publicdomain/mark/1.0"},
        {"http://creativecommons.org/licenses/publicdomain/", "pd", "Public domain",
         "creativecommons.org/licenses/publicdomain"},
        {"http://example.com/licenses/by/3.0/", "-", "", ""},
        {"http://creativecommons.org/licenses/sampling+/1.0/", "-", "", ""},
        {"http://creativecommons.org/licenses/by/3.0x/", "by", "CC BY", "creativecommons.org/licenses/by"},
        {"", "-", "", ""},
        {NULL, "-", "", ""},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        char code[STREAM_LICENSE_MAX], name[96], link[160];
        streamLicenseCode(CASES[i].url, code, sizeof(code));
        CHECK_STR(code, CASES[i].code);
        streamLicenseName(code, name, sizeof(name));
        CHECK_STR(name, CASES[i].name);
        streamLicenseLink(code, link, sizeof(link));
        CHECK_STR(link, CASES[i].link);
    }
    char name[96], link[96], line[256];
    streamLicenseName("lma", name, sizeof(name));
    CHECK_STR(name, "Live Music Archive: non-commercial");
    streamLicenseLink("lma", link, sizeof(link));
    CHECK_STR(link, "archive.org/details/etree");
    streamLicenseName("78rpm", name, sizeof(name));
    CHECK_STR(name, "78 rpm collection");
    CHECK(streamCreditLine("ia:/ndb.x/by-4.0/a.mp3", line, sizeof(line)));
    CHECK_STR(line, "CC BY 4.0  \xC2\xB7  Internet Archive  \xC2\xB7  archive.org/details/ndb.x");
    CHECK(streamCreditLine("ia:/ndb.x/-/a.mp3", line, sizeof(line)));
    CHECK_STR(line, "Internet Archive  \xC2\xB7  archive.org/details/ndb.x");
    CHECK(!streamCreditLine("ia:/ndb.x/cover/300.jpg", line, sizeof(line)));
    CHECK(!streamCreditLine("sdmc:/a.mp3", line, sizeof(line)));
}

static void testUrls(void)
{
    char url[STREAM_URL_MAX];
    StreamQuery q = {.kind = STREAM_Q_POPULAR_LIVE};
    CHECK(streamQueryUrl(&q, url, sizeof(url)));
    CHECK_STR(url, "https://archive.org/advancedsearch.php?q=collection%3Aetree%20AND%20mediatype%3Aetree"
                   "&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=creator&fl%5B%5D=date&fl%5B%5D=licenseurl"
                   "&sort%5B%5D=downloads+desc&rows=50&page=1&output=json");
    q.page = 3;  // "Show 50 more", twice.
    CHECK(streamQueryUrl(&q, url, sizeof(url)) && strstr(url, "&rows=50&page=3&"));
    q.page = 11;  // Past STREAM_ITEMS_MAX: refused.
    CHECK(!streamQueryUrl(&q, url, sizeof(url)));
    q.page = 0;
    q.kind = STREAM_Q_POPULAR_78;
    CHECK(streamQueryUrl(&q, url, sizeof(url)) && strstr(url, "q=collection%3A78rpm%20AND%20mediatype%3Aaudio&"));
    q.kind = STREAM_Q_CLASSICAL;
    CHECK(streamQueryUrl(&q, url, sizeof(url)) && strstr(url, "subject%3Aclassical") && strstr(url, "downloads+desc"));
    q = (StreamQuery){.kind = STREAM_Q_SEARCH_LIVE};
    snprintf(q.words, sizeof(q.words), "Night* Drive AND (1977)");
    CHECK(streamQueryUrl(&q, url, sizeof(url)));
    CHECK(strstr(url, "%20AND%20%28night%20drive%20and%201977%29") != NULL && !strstr(url, "sort"));
    q.kind = STREAM_Q_SEARCH_78;
    CHECK(streamQueryUrl(&q, url, sizeof(url)) && strstr(url, "78rpm"));
    q.kind = STREAM_Q_SEARCH_FREE;
    snprintf(q.words, sizeof(q.words), "*** ()");
    CHECK(!streamQueryUrl(&q, url, sizeof(url)));  // Nothing left to search for.
    char words[32];
    CHECK(streamSearchWords("  \"Señales\" -- OR: x ", words, sizeof(words)));
    CHECK_STR(words, "señales or x");
    CHECK(streamSearchWords("ab\xC3\xB1", words, 4) && !strcmp(words, "ab"));  // A character cut: left out.
    q = (StreamQuery){.kind = STREAM_Q_ITEM};
    snprintf(q.id, sizeof(q.id), "ndb.x");
    CHECK(streamQueryUrl(&q, url, sizeof(url)));
    CHECK_STR(url, "https://archive.org/metadata/ndb.x");
    snprintf(q.id, sizeof(q.id), "../x");
    CHECK(!streamQueryUrl(&q, url, sizeof(url)));
    q.kind = STREAM_Q_COUNT;
    CHECK(!streamQueryUrl(&q, url, sizeof(url)) && !streamQueryUrl(NULL, url, sizeof(url)));
    CHECK(!streamQueryUrl(&(StreamQuery){.kind = STREAM_Q_POPULAR_LIVE}, url, 40));  // Doesn't fit.

    CHECK(streamMenuCount() == 6);
    for (int row = 0; row < streamMenuCount(); row++)
        CHECK(streamMenuLabel(row)[0] && streamMenuQuery(row) < STREAM_Q_COUNT);
    CHECK(streamMenuSearches(1) && !streamMenuSearches(0) && !streamMenuSearches(99));
    CHECK(streamMenuQuery(-1) == STREAM_Q_COUNT && streamMenuLabel(6)[0] == '\0');
}

static void testAnswers(void)
{
    StreamList list = {0};
    char why[256];
    StreamQuery q = {.kind = STREAM_Q_POPULAR_LIVE};
    CHECK(parseFixture("archive_search.json", &q, &list, why, sizeof(why)));
    CHECK(list.count == 2 && list.total == 307208);
    if (list.count == 2) {
        CHECK(list.items[0].kind == STREAM_ITEM_ARCHIVE);
        CHECK_STR(list.items[0].id, "ndb2006-01-14.made.up");
        CHECK_STR(list.items[0].artist, "The Night Drive Band");
        CHECK_STR(list.items[0].detail, "2006-01-14");
        CHECK_STR(list.items[0].location, "ia:/ndb2006-01-14.made.up/cover/0.jpg");
        CHECK_STR(list.items[1].title, "af1999-07-01.sbd");  // No title: its identifier.
        CHECK_STR(list.items[1].detail, "1999");
    }

    q = (StreamQuery){.kind = STREAM_Q_ITEM};
    snprintf(q.id, sizeof(q.id), "ndb2006-01-14.made.up");
    CHECK(parseFixture("archive_item.json", &q, &list, why, sizeof(why)));
    CHECK(list.count == 4);  // The VBR MP3s, without the one with a control character.
    if (list.count == 4) {
        CHECK_STR(list.items[0].title, "Opening");
        CHECK(list.items[0].position == 1);
        CHECK_STR(list.items[0].artist, "The Night Drive Band");
        CHECK_STR(list.items[0].album, "The Night Drive Band Live at a Made-Up Hall");
        CHECK_STR(list.items[0].location, "ia:/ndb2006-01-14.made.up/lma/ndb2006-01-14d1t01.mp3");
        CHECK_STR(list.items[1].title, "Second Song");
        CHECK_STR(list.items[2].title, "ndb2006-01-14d1t04");
        CHECK_STR(list.items[3].title, "ndb d2t01 encore #1");
        CHECK_STR(list.items[3].location, "ia:/ndb2006-01-14.made.up/lma/disc%202%2Fndb%20d2t01%20encore%20%231.mp3");
    }
    snprintf(q.id, sizeof(q.id), "madeup-netlabel-001");
    CHECK(parseFixture("archive_item_cc.json", &q, &list, why, sizeof(why)) && list.count == 1);
    CHECK(list.count == 1 && !strcmp(list.items[0].location, "ia:/madeup-netlabel-001/by-sa-4.0/01%20-%20Quiet%20Room.ogg"));
    snprintf(q.id, sizeof(q.id), "78_made-up-waltz");
    CHECK(parseFixture("archive_item_78.json", &q, &list, why, sizeof(why)) && list.count == 2);
    if (list.count == 2) {  // No license said: the 78 rpm collection's.
        CHECK_STR(list.items[0].location, "ia:/78_made-up-waltz/78rpm/Made-Up%20Waltz%20-%20Side%20A.mp3");
        CHECK_STR(list.items[0].artist, "The Paper Moon Orchestra");  // The file's own creator.
    }

    CHECK(!parseText("{}", &q, &list, why, sizeof(why)));
    CHECK_STR(why, "That item isn't on the Internet Archive any more.");
    CHECK(!parseText("{\"metadata\":[],\"files\":[]}", &q, &list, why, sizeof(why)) && why[0]);
    CHECK(parseText("{\"metadata\":{},\"files\":[{\"name\":\"a.txt\",\"format\":\"Text\"}]}", &q, &list, why,
                    sizeof(why)) && list.count == 0);
    q.kind = STREAM_Q_SEARCH_FREE;
    CHECK(!parseText("{\"error\":\"Bad query\"}", &q, &list, why, sizeof(why)));
    CHECK_STR(why, "The Internet Archive said: Bad query");
    CHECK(!parseText("[1,2]", &q, &list, why, sizeof(why)) && why[0] && list.count == 0);
    CHECK(parseText("{\"response\":{\"numFound\":0,\"docs\":[]}}", &q, &list, why, sizeof(why)) && list.count == 0);
    streamListFree(&list);
}

static void testMetaStore(void)
{
    char title[64], artist[64], album[64];
    CHECK(!streamMetaGet("ia:/x/-/1.mp3", title, sizeof(title), artist, sizeof(artist), album, sizeof(album)));
    CHECK(streamMetaPut("ia:/x/-/1.mp3", "A", "B", "C"));
    CHECK(streamMetaPut("ia:/x/-/1.mp3", "A2", "B", NULL));  // Replaced, not added again.
    CHECK(streamMetaGet("ia:/x/-/1.mp3", title, sizeof(title), artist, sizeof(artist), album, sizeof(album)));
    CHECK_STR(title, "A2");
    CHECK_STR(album, "");
    char loc[64];
    for (int i = 0; i < STREAM_META_MAX; i++) {  // The oldest goes.
        snprintf(loc, sizeof(loc), "ia:/x/-/%d.mp3", 100 + i);
        CHECK(streamMetaPut(loc, "x", "y", "z"));
    }
    CHECK(!streamMetaGet("ia:/x/-/1.mp3", title, sizeof(title), artist, sizeof(artist), album, sizeof(album)));
    CHECK(streamMetaGet(loc, title, 2, artist, sizeof(artist), album, sizeof(album)) && !strcmp(title, "x"));
    CHECK(!streamMetaPut("", "x", "y", "z"));
    streamMetaClear();
    CHECK(!streamMetaGet(loc, title, sizeof(title), artist, sizeof(artist), album, sizeof(album)));
}

void testStream(void)
{
    testLocations();
    testLicenses();
    testUrls();
    testAnswers();
    testMetaStore();
}
