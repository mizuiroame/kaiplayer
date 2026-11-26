// Online music (stream.h, streammeta.h): locations, licenses, the requests' URLs, and the services'
// answers, from made-up fixtures (never the real services).
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
    char *data = malloc((size_t)n + 1);
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

// The fixture's answer to q into list; returns whether it parsed, with why.
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

static void testLocations(void)
{
    char loc[UTIL_PATH_MAX], cover[UTIL_PATH_MAX];
    StreamTarget t;
    CHECK(streamJamendoLocation("70001", "http://creativecommons.org/licenses/by-nc-sa/3.0/", "1000001", loc,
                                sizeof(loc)));
    CHECK_STR(loc, "jam:/70001/by-nc-sa-3.0/1000001.mp3");
    CHECK(streamIsLocation(loc) && !streamIsLocation("sub:/x/1.mp3") && !streamIsLocation(NULL));
    CHECK(streamParseLocation(loc, &t) && t.service == STREAM_JAMENDO && !t.cover);
    CHECK_STR(t.key, "70001");
    CHECK_STR(t.license, "by-nc-sa-3.0");
    CHECK_STR(t.id, "1000001");
    CHECK(streamJamendoLocation("", "", "42", loc, sizeof(loc)));  // A single, no license said.
    CHECK_STR(loc, "jam:/-/-/42.mp3");
    CHECK(!streamCoverLocation(loc, 300, cover, sizeof(cover)));   // A single has no album cover.
    CHECK(!streamJamendoLocation("1", "", "../1", loc, sizeof(loc)));
    CHECK(!streamJamendoLocation("a1", "", "1", loc, sizeof(loc)));
    CHECK(!streamJamendoLocation("1", "", "1", loc, 10));  // Doesn't fit.

    CHECK(streamArchiveLocation("ndb2006-01-14.made.up", "lma", "disc 2/song #1.mp3", loc, sizeof(loc)));
    CHECK_STR(loc, "ia:/ndb2006-01-14.made.up/lma/disc%202%2Fsong%20%231.mp3");
    CHECK(streamParseLocation(loc, &t) && t.service == STREAM_ARCHIVE);
    CHECK_STR(t.key, "ndb2006-01-14.made.up");
    CHECK_STR(t.file, "disc 2/song #1.mp3");
    CHECK(!streamArchiveLocation("bad id", "lma", "a.mp3", loc, sizeof(loc)));
    CHECK(!streamArchiveLocation("..", "lma", "a.mp3", loc, sizeof(loc)));
    CHECK(!streamArchiveLocation("ok", "lma", "notes.txt", loc, sizeof(loc)));
    CHECK(!streamArchiveLocation("ok", "LMA", "a.mp3", loc, sizeof(loc)));
    CHECK(!streamArchiveLocation("ok", "lma", "a\x01.mp3", loc, sizeof(loc)));

    // Covers: a song's, and the keys' of album, artist and item rows.
    CHECK(streamCoverLocation("jam:/70001/by-3.0/5.mp3", 256, cover, sizeof(cover)));
    CHECK_STR(cover, "jam:/70001/cover/256.jpg");
    CHECK(streamParseLocation(cover, &t) && t.cover && t.px == 256);
    char url[STREAM_URL_MAX];
    CHECK(streamCoverUrl(&t, url, sizeof(url)));
    CHECK_STR(url, "https://usercontent.jamendo.com/?type=album&id=70001&width=300");
    CHECK(streamCoverKeyLocation(STREAM_JAMENDO, "a5001", cover, sizeof(cover)));
    CHECK(streamCoverLocation(cover, 700, cover, sizeof(cover)) && streamParseLocation(cover, &t));
    CHECK(streamCoverUrl(&t, url, sizeof(url)));
    CHECK_STR(url, "https://usercontent.jamendo.com/?type=artist&id=5001&width=600");
    CHECK(streamCoverKeyLocation(STREAM_ARCHIVE, "ndb.x", cover, sizeof(cover)) && streamParseLocation(cover, &t));
    CHECK(streamCoverUrl(&t, url, sizeof(url)));
    CHECK_STR(url, "https://archive.org/services/img/ndb.x");
    CHECK(!streamCoverKeyLocation(STREAM_JAMENDO, "-", cover, sizeof(cover)));

    static const char *const BAD[] = {
        "jam:/", "jam://", "jam:/1/by/", "jam:/1/by/x.mp3", "jam:/1/BY/1.mp3", "jam:/1/by/1.ogg", "jam:/a1/by/1.mp3",
        "jam:/-/cover/1.jpg", "jam:/1/cover/99999.jpg", "jam:/1/cover/x.jpg", "ia:/x/lma/a%2.mp3", "ia:/x/lma/a%00.mp3",
        "ia:/x/lma/a.txt", "ia:/../lma/a.mp3", "ia:/x/lma/a/b.mp3", "ia:/x//a.mp3", "sdmc:/music/a.mp3",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
        CHECK(!streamParseLocation(BAD[i], &t));

    // Playlists keep these locations as they are.
    TrackList list = {0};
    CHECK(trackListAdd(&list, "jam:/70001/by-nc-sa-3.0/1000001.mp3"));
    CHECK(trackListAdd(&list, "ia:/ndb.x/lma/disc%202%2Fsong%20%231.mp3"));
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
    char name[96], line[256];
    streamLicenseName("lma", name, sizeof(name));
    CHECK_STR(name, "Live Music Archive: non-commercial");
    CHECK(streamCreditLine("jam:/70001/by-nc-sa-3.0/1000001.mp3", line, sizeof(line)));
    CHECK_STR(line, "CC BY-NC-SA 3.0  \xC2\xB7  Jamendo  \xC2\xB7  jamendo.com/track/1000001");
    CHECK(streamCreditLine("ia:/ndb.x/-/a.mp3", line, sizeof(line)));
    CHECK_STR(line, "Internet Archive  \xC2\xB7  archive.org/details/ndb.x");
    CHECK(!streamCreditLine("jam:/70001/cover/300.jpg", line, sizeof(line)));
    CHECK(!streamCreditLine("sdmc:/a.mp3", line, sizeof(line)));
}

static void testUrls(void)
{
    char url[STREAM_URL_MAX];
    StreamQuery q = {.kind = STREAM_Q_JAM_POPULAR};
    CHECK(streamQueryUrl(&q, "abc123", url, sizeof(url)));
    CHECK_STR(url, "https://api.jamendo.com/v3.0/tracks/?client_id=abc123&format=json&limit=50"
                   "&order=popularity_month&audioformat=mp32");
    CHECK(!streamQueryUrl(&q, "", url, sizeof(url)));
    CHECK(!streamQueryUrl(&q, "abc&x=1", url, sizeof(url)));
    CHECK(!streamClientIdValid("0123456789012345678901234567890123456789012345678901234567890123"));
    q = (StreamQuery){.kind = STREAM_Q_JAM_SEARCH_TRACKS};
    snprintf(q.words, sizeof(q.words), "  lo-fi & rain  ");
    CHECK(streamQueryUrl(&q, "abc123", url, sizeof(url)));
    CHECK(strstr(url, "&search=lo-fi%20%26%20rain&") != NULL);
    snprintf(q.words, sizeof(q.words), "   ");
    CHECK(!streamQueryUrl(&q, "abc123", url, sizeof(url)));
    q = (StreamQuery){.kind = STREAM_Q_JAM_ALBUM_TRACKS};
    snprintf(q.id, sizeof(q.id), "70001");
    CHECK(streamQueryUrl(&q, "abc123", url, sizeof(url)) && strstr(url, "album_id=70001&order=position"));
    snprintf(q.id, sizeof(q.id), "7&x");
    CHECK(!streamQueryUrl(&q, "abc123", url, sizeof(url)));
    q = (StreamQuery){.kind = STREAM_Q_JAM_SEARCH_ARTISTS};
    snprintf(q.words, sizeof(q.words), "Línea");
    CHECK(streamQueryUrl(&q, "abc123", url, sizeof(url)) && strstr(url, "artists/?") &&
          strstr(url, "namesearch=L%C3%ADnea"));

    q = (StreamQuery){.kind = STREAM_Q_IA_POPULAR_LIVE};
    CHECK(streamQueryUrl(&q, NULL, url, sizeof(url)));
    CHECK_STR(url, "https://archive.org/advancedsearch.php?q=collection%3Aetree%20AND%20mediatype%3Aetree"
                   "&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=creator&fl%5B%5D=date&fl%5B%5D=licenseurl"
                   "&sort%5B%5D=downloads+desc&rows=50&page=1&output=json");
    q = (StreamQuery){.kind = STREAM_Q_IA_SEARCH_LIVE};
    snprintf(q.words, sizeof(q.words), "Night* Drive AND (1977)");
    CHECK(streamQueryUrl(&q, NULL, url, sizeof(url)));
    CHECK(strstr(url, "%20AND%20%28night%20drive%20and%201977%29") != NULL);
    char words[32];
    CHECK(streamArchiveWords("  \"Señales\" -- OR: x ", words, sizeof(words)));
    CHECK_STR(words, "señales or x");
    CHECK(!streamArchiveWords("*** ()", words, sizeof(words)));
    q = (StreamQuery){.kind = STREAM_Q_IA_ITEM};
    snprintf(q.id, sizeof(q.id), "ndb.x");
    CHECK(streamQueryUrl(&q, NULL, url, sizeof(url)));
    CHECK_STR(url, "https://archive.org/metadata/ndb.x");
    snprintf(q.id, sizeof(q.id), "../x");
    CHECK(!streamQueryUrl(&q, NULL, url, sizeof(url)));
    CHECK(streamArchiveFileUrl("ndb.x", "disc 2/a #1.mp3", url, sizeof(url)));
    CHECK_STR(url, "https://archive.org/download/ndb.x/disc%202/a%20%231.mp3");
    q.kind = STREAM_Q_COUNT;
    CHECK(!streamQueryUrl(&q, "abc", url, sizeof(url)));
}

static void testJamendoAnswers(void)
{
    StreamList list = {0};
    char why[256];
    StreamQuery q = {.kind = STREAM_Q_JAM_POPULAR};
    CHECK(parseFixture("jamendo_tracks.json", &q, &list, why, sizeof(why)));
    CHECK(list.count == 3 && list.total == 4);  // The one with a bad id is left out.
    if (list.count == 3) {
        CHECK(list.items[0].kind == STREAM_ITEM_TRACK && list.items[0].position == 1);
        CHECK_STR(list.items[0].title, "Aurora Fields");
        CHECK_STR(list.items[0].artist, "The Night Drive Band");
        CHECK_STR(list.items[0].album, "Made-Up Skies");
        CHECK_STR(list.items[0].detail, "2021");
        CHECK_STR(list.items[0].location, "jam:/70001/by-nc-sa-3.0/1000001.mp3");
        CHECK_STR(list.items[1].title, "Señales nocturnas");  // A control character, a space.
        CHECK_STR(list.items[1].location, "jam:/-/by-4.0/1000002.mp3");
        CHECK_STR(list.items[2].title, "1000004");  // No name: its id.
        CHECK_STR(list.items[2].location, "jam:/70002/-/1000004.mp3");
        CHECK(list.items[2].position == 3 && list.items[2].detail[0] == '\0');
    }
    q.kind = STREAM_Q_JAM_NEW_ALBUMS;
    CHECK(parseFixture("jamendo_albums.json", &q, &list, why, sizeof(why)) && list.count == 2);
    if (list.count == 2) {
        CHECK(list.items[0].kind == STREAM_ITEM_ALBUM);
        CHECK_STR(list.items[0].id, "70001");
        CHECK_STR(list.items[0].location, "jam:/70001/cover/0.jpg");
        CHECK_STR(list.items[1].artist, "Línea Ficticia");
    }
    q.kind = STREAM_Q_JAM_SEARCH_ARTISTS;
    CHECK(parseFixture("jamendo_artists.json", &q, &list, why, sizeof(why)) && list.count == 1);
    CHECK(list.count == 1 && list.items[0].kind == STREAM_ITEM_ARTIST && !strcmp(list.items[0].location, "jam:/a5001/cover/0.jpg"));
    q.kind = STREAM_Q_JAM_POPULAR;
    CHECK(!parseFixture("jamendo_error.json", &q, &list, why, sizeof(why)) && list.count == 0);
    CHECK_STR(why, "Jamendo didn't take the client ID: check it in Settings, Online music.");

    size_t size;
    char *text = readFixture("jamendo_audio.json", &size), url[STREAM_URL_MAX];
    JsonDoc *doc = text ? jsonParse(text, size) : NULL;
    CHECK(doc && streamJamendoAudioUrl(jsonRoot(doc), url, sizeof(url)));
    CHECK_STR(url, "https://prod-1.storage.jamendo.com/?trackid=1000001&format=mp32&from=app-test");
    CHECK(!streamJamendoAudioUrl(jsonRoot(doc), url, 20));  // Doesn't fit.
    jsonFree(doc);
    free(text);
    static const char *const BAD[] = {
        "{\"headers\":{\"status\":\"success\"},\"results\":[{\"audio\":\"http://plain.example/a\"}]}",
        "{\"headers\":{\"status\":\"success\"},\"results\":[{\"audio\":\"https://a b\"}]}",
        "{\"headers\":{\"status\":\"success\"},\"results\":[]}",
        "{\"headers\":{\"status\":\"failed\",\"code\":11},\"results\":[]}",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        doc = jsonParse(BAD[i], strlen(BAD[i]));
        CHECK(doc && !streamJamendoAudioUrl(jsonRoot(doc), url, sizeof(url)));
        jsonFree(doc);
    }
    static const char SUSPENDED[] = "{\"headers\":{\"status\":\"failed\",\"code\":11,\"error_message\":\"x\"},\"results\":[]}";
    doc = jsonParse(SUSPENDED, strlen(SUSPENDED));
    CHECK(doc && !streamParseAnswer(&q, jsonRoot(doc), &list, why, sizeof(why)));
    CHECK_STR(why, "Jamendo has suspended this client ID.");
    jsonFree(doc);
    static const char OTHER[] = "{\"headers\":{\"status\":\"failed\",\"code\":2,\"error_message\":\"Bad\\tthing\"},\"results\":[]}";
    doc = jsonParse(OTHER, strlen(OTHER));
    CHECK(doc && !streamParseAnswer(&q, jsonRoot(doc), &list, why, sizeof(why)));
    CHECK_STR(why, "Jamendo said: Bad thing");
    jsonFree(doc);
    static const char WRONG[] = "[1,2]";
    doc = jsonParse(WRONG, strlen(WRONG));
    CHECK(doc && !streamParseAnswer(&q, jsonRoot(doc), &list, why, sizeof(why)) && why[0]);
    jsonFree(doc);
    streamListFree(&list);
}

static void testArchiveAnswers(void)
{
    StreamList list = {0};
    char why[256];
    StreamQuery q = {.kind = STREAM_Q_IA_POPULAR_LIVE};
    CHECK(parseFixture("archive_search.json", &q, &list, why, sizeof(why)));
    CHECK(list.count == 2 && list.total == 307208);
    if (list.count == 2) {
        CHECK(list.items[0].kind == STREAM_ITEM_ARCHIVE);
        CHECK_STR(list.items[0].id, "ndb2006-01-14.made.up");
        CHECK_STR(list.items[0].detail, "2006-01-14");
        CHECK_STR(list.items[0].location, "ia:/ndb2006-01-14.made.up/cover/0.jpg");
        CHECK_STR(list.items[1].title, "af1999-07-01.sbd");  // No title: its identifier.
        CHECK_STR(list.items[1].detail, "1999");
    }
    q.kind = STREAM_Q_IA_TEST;
    CHECK(parseFixture("archive_search.json", &q, &list, why, sizeof(why)) && list.count == 0 && list.number == 307208);

    q = (StreamQuery){.kind = STREAM_Q_IA_ITEM};
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
    static const char EMPTY[] = "{}", ERROR[] = "{\"error\":\"Bad query\"}";
    JsonDoc *doc = jsonParse(EMPTY, strlen(EMPTY));
    CHECK(doc && !streamParseAnswer(&q, jsonRoot(doc), &list, why, sizeof(why)));
    CHECK_STR(why, "That item isn't on the Internet Archive any more.");
    jsonFree(doc);
    q.kind = STREAM_Q_IA_SEARCH_FREE;
    doc = jsonParse(ERROR, strlen(ERROR));
    CHECK(doc && !streamParseAnswer(&q, jsonRoot(doc), &list, why, sizeof(why)));
    CHECK_STR(why, "The Internet Archive said: Bad query");
    jsonFree(doc);
    streamListFree(&list);
}

static void testMetaStore(void)
{
    char title[64], artist[64], album[64];
    CHECK(!streamMetaGet("jam:/1/-/1.mp3", title, sizeof(title), artist, sizeof(artist), album, sizeof(album)));
    CHECK(streamMetaPut("jam:/1/-/1.mp3", "A", "B", "C"));
    CHECK(streamMetaPut("jam:/1/-/1.mp3", "A2", "B", NULL));  // Replaced, not added again.
    CHECK(streamMetaGet("jam:/1/-/1.mp3", title, sizeof(title), artist, sizeof(artist), album, sizeof(album)));
    CHECK_STR(title, "A2");
    CHECK_STR(album, "");
    char loc[64];
    for (int i = 0; i < STREAM_META_MAX; i++) {  // The oldest goes.
        snprintf(loc, sizeof(loc), "jam:/1/-/%d.mp3", 100 + i);
        CHECK(streamMetaPut(loc, "x", "y", "z"));
    }
    CHECK(!streamMetaGet("jam:/1/-/1.mp3", title, sizeof(title), artist, sizeof(artist), album, sizeof(album)));
    CHECK(streamMetaGet(loc, title, 2, artist, sizeof(artist), album, sizeof(album)) && !strcmp(title, "x"));
    CHECK(!streamMetaPut("", "x", "y", "z"));
    streamMetaClear();
    CHECK(!streamMetaGet(loc, title, sizeof(title), artist, sizeof(artist), album, sizeof(album)));
}

static void testGenres(void)
{
    char url[STREAM_URL_MAX];
    for (int i = 0; i < STREAM_GENRE_COUNT; i++) {
        StreamQuery q = {.kind = STREAM_Q_JAM_GENRE};
        snprintf(q.words, sizeof(q.words), "%s", streamGenreTag(i));
        CHECK(streamGenreName(i)[0] && streamQueryUrl(&q, "abc", url, sizeof(url)));
    }
    CHECK(streamGenreTag(-1)[0] == '\0' && streamGenreName(STREAM_GENRE_COUNT)[0] == '\0');
}

void testStream(void)
{
    testLocations();
    testLicenses();
    testUrls();
    testJamendoAnswers();
    testArchiveAnswers();
    testMetaStore();
    testGenres();
}
