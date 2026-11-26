#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "json.h"
#include "md5.h"
#include "subsonic.h"

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

static void testMd5(void)
{
    char hex[MD5_HEX_SIZE];
    md5Hex("", 0, hex);
    CHECK_STR(hex, "d41d8cd98f00b204e9800998ecf8427e");
    md5Hex("abc", 3, hex);
    CHECK_STR(hex, "900150983cd24fb0d6963f7d28e17f72");
    const char *long_text = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    md5Hex(long_text, strlen(long_text), hex);  // Over a block, across the padding's edge.
    CHECK_STR(hex, "57edf4a22be3c955ac49da2e2107b67a");
    // The Subsonic API's own example: password "sesame", salt "c19b2d".
    md5Hex("sesamec19b2d", 12, hex);
    CHECK_STR(hex, "26719a1196d2a940705a59634eb18eab");
}

static void testJson(void)
{
    static const char TEXT[] = " {\"a\": 1, \"b\": [true, false, null, -2.5e1], \"c\": {\"d\": \"x\\\"y\\u00e9\\ud83d\\ude00\"},"
                               " \"n\": \"42\", \"e\": [], \"o\": {}} ";
    JsonDoc *doc = jsonParse(TEXT, strlen(TEXT));
    CHECK(doc != NULL);
    const JsonValue *root = jsonRoot(doc);
    CHECK(root && root->type == JSON_OBJECT);
    CHECK(jsonInt(root, "a") == 1 && jsonHasNumber(root, "a") && !jsonHasNumber(root, "n"));
    CHECK(jsonInt(root, "n") == 42 && jsonInt(root, "missing") == 0);
    const JsonValue *b = jsonGet(root, "b");
    CHECK(b && b->type == JSON_ARRAY && b->child && b->child->type == JSON_TRUE);
    CHECK(b->child->next->type == JSON_FALSE && b->child->next->next->type == JSON_NULL);
    CHECK(b->child->next->next->next->number == -25.0 && !b->child->next->next->next->next);
    CHECK_STR(jsonText(jsonGet(root, "c"), "d"), "x\"y\xC3\xA9\xF0\x9F\x98\x80");
    CHECK_STR(jsonText(root, "a"), "");  // Not a string.
    CHECK(jsonGet(root, "e")->child == NULL && jsonGet(root, "o")->child == NULL);
    jsonFree(doc);

    static const char *const BAD[] = {
        "", "{", "{\"a\":}", "[1,]", "[1 2]", "{\"a\" 1}", "\"open", "01", "1.", "-", "tru", "[1]x",
        "\"tab\tin\"", "\"\\q\"", "\"\\u12\"", "{1:2}", "nul",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        JsonDoc *d = jsonParse(BAD[i], strlen(BAD[i]));
        CHECK(d == NULL);
        jsonFree(d);
    }
    char deep[JSON_DEPTH_MAX * 2 + 8];  // Too deep: refused, not a stack overflow.
    size_t n = 0;
    for (int i = 0; i < JSON_DEPTH_MAX + 2; i++)
        deep[n++] = '[';
    for (int i = 0; i < JSON_DEPTH_MAX + 2; i++)
        deep[n++] = ']';
    CHECK(jsonParse(deep, n) == NULL);
    // Too many values: refused. Exactly the most is fine.
    size_t many_size = (size_t)JSON_VALUES_MAX * 2 + 2;
    char *many = malloc(many_size + 1);
    if (many) {
        many[0] = '[';
        for (size_t i = 0; i < JSON_VALUES_MAX - 1; i++) {  // The array is a value too.
            many[1 + 2 * i] = '0';
            many[2 + 2 * i] = ',';
        }
        many[2 * (JSON_VALUES_MAX - 1)] = ']';
        doc = jsonParse(many, 2 * (JSON_VALUES_MAX - 1) + 1);
        CHECK(doc != NULL);
        jsonFree(doc);
        many[2 * (JSON_VALUES_MAX - 1)] = ',';
        many[2 * (JSON_VALUES_MAX - 1) + 1] = '0';
        many[2 * (JSON_VALUES_MAX - 1) + 2] = ']';
        CHECK(jsonParse(many, 2 * (JSON_VALUES_MAX - 1) + 3) == NULL);
        free(many);
    }
    static const char LONE[] = "\"\\ud800!\"";  // Half a surrogate pair: the replacement character.
    doc = jsonParse(LONE, strlen(LONE));
    CHECK(doc && !strcmp(jsonRoot(doc)->text, "\xEF\xBF\xBD!"));
    jsonFree(doc);
    CHECK(jsonParse("[1]\0", 4) == NULL);  // Text after the value, even a NUL.
}

static void testAddresses(void)
{
    char out[SUB_ADDRESS_MAX];
    CHECK(subNormalizeAddress(" 192.168.0.10:4533 ", out, sizeof(out)));
    CHECK_STR(out, "http://192.168.0.10:4533");
    CHECK(subNormalizeAddress("https://music.example.com/navidrome/", out, sizeof(out)));
    CHECK_STR(out, "https://music.example.com/navidrome");
    CHECK(subNormalizeAddress("http://pc:4533/app/#/album/recentlyAdded?sort=x", out, sizeof(out)));
    CHECK_STR(out, "http://pc:4533");
    CHECK(subNormalizeAddress("pc:4533/rest/", out, sizeof(out)));
    CHECK_STR(out, "http://pc:4533");
    CHECK(subNormalizeAddress("HTTPS://Box", out, sizeof(out)));
    CHECK_STR(out, "HTTPS://Box");
    CHECK(!subNormalizeAddress("", out, sizeof(out)));
    CHECK(!subNormalizeAddress("http://", out, sizeof(out)));
    CHECK(!subNormalizeAddress("my pc:4533", out, sizeof(out)));
    CHECK(!subNormalizeAddress("smb://pc/Music", out, sizeof(out)));
    CHECK(!subNormalizeAddress("http://me:secret@pc:4533", out, sizeof(out)));
    CHECK(!subNormalizeAddress("/rest", out, sizeof(out)));
    subShownAddress("http://pc:4533", out, sizeof(out));
    CHECK_STR(out, "pc:4533");
    subShownAddress("https://pc", out, sizeof(out));
    CHECK_STR(out, "https://pc");
    CHECK(subSameServer("http://pc:4533/a", "HTTP://PC:4533"));
    CHECK(!subSameServer("http://pc:4533", "https://pc:4533"));
    CHECK(!subSameServer("http://pc:4533", "http://pc:4534"));

    // Navidrome under a path (its BaseUrl), typed without it, sends the API's request to its page there.
    CHECK(subPointedAddress("http://pc:4534", "http://pc:4534/navidrome/app/", out, sizeof(out)));
    CHECK_STR(out, "http://pc:4534/navidrome");
    CHECK(subPointedAddress("http://pc:4534", "https://pc:4534/a/navidrome/app?x=1#/album", out, sizeof(out)));
    CHECK_STR(out, "https://pc:4534/a/navidrome");
    CHECK(subPointedAddress("http://pc:4534/old", "http://pc:4534/music/APP", out, sizeof(out)));
    CHECK_STR(out, "http://pc:4534/music");
    CHECK(!subPointedAddress("http://pc:4533", "http://pc:4533/app/", out, sizeof(out)));  // At the top: no path.
    CHECK(!subPointedAddress("http://pc:4534/navidrome", "http://pc:4534/navidrome/app/", out, sizeof(out)));
    CHECK(!subPointedAddress("http://pc:4534", "http://pc:4534/login", out, sizeof(out)));  // Not Navidrome's page.
    CHECK(!subPointedAddress("http://pc:4534", "http://pc:4534/myapp/", out, sizeof(out)));
    CHECK(!subPointedAddress("http://pc:4534", "", out, sizeof(out)));  // No redirect.
    CHECK(!subPointedAddress("http://pc:4534", "/navidrome/app/", out, sizeof(out)));
    char small[8];
    CHECK(!subPointedAddress("http://pc:4534", "http://pc:4534/navidrome/app/", small, sizeof(small)));
}

static void testAccount(void)
{
    SubAccount a = {.address = "http://pc:4533", .user = "kai pl&yer", .check_certificate = false}, b;
    CHECK(subSetPassword(&a, "sesame", "c19b2d"));
    CHECK_STR(a.token, "26719a1196d2a940705a59634eb18eab");
    CHECK(subSignedIn(&a));
    CHECK(!subSetPassword(&b, "x", ""));
    CHECK(!subSetPassword(&b, "x", "a&b"));
    char text[1024];
    CHECK(subAccountWrite(&a, text, sizeof(text)));
    CHECK(!strstr(text, "sesame"));  // Never the password itself.
    subAccountRead(&b, text);
    CHECK_STR(b.address, a.address);
    CHECK_STR(b.user, a.user);
    CHECK_STR(b.salt, a.salt);
    CHECK_STR(b.token, a.token);
    CHECK(!b.check_certificate && subSignedIn(&b));
    subAccountRead(&b, "address=pc:4533\r\nuser=u\r\n");
    CHECK_STR(b.address, "http://pc:4533");
    CHECK(b.check_certificate && !subSignedIn(&b));
    subAccountRead(&b, "address=http://pc\nuser=u\nsalt=a&b\ntoken=26719a1196d2a940705a59634eb18eab\n");
    CHECK(!subSignedIn(&b));  // A damaged salt: signed out, not sent.
    SubAccount bad = {.address = "http://pc", .user = "line\nbreak"};
    CHECK(!subAccountWrite(&bad, text, sizeof(text)));

    char url[1024];
    CHECK(subRequestUrl(&a, "ping", NULL, url, sizeof(url)));
    CHECK_STR(url, "http://pc:4533/rest/ping?u=kai%20pl%26yer&t=26719a1196d2a940705a59634eb18eab&s=c19b2d"
                   "&v=1.16.1&c=KaiPlayer&f=json");
    CHECK(!strstr(url, "p="));
    CHECK(subRequestUrl(&a, "search3", "query=&songCount=500", url, sizeof(url)));
    CHECK(strstr(url, "&f=json&query=&songCount=500") != NULL);
    CHECK(!subRequestUrl(&a, "ping", NULL, url, 40));
}

// OpenSubsonic's apiKeyAuthentication: the key alone signs in, never with a user.
static void testApiKey(void)
{
    SubAccount a = {.address = "http://pc:4533", .user = "kai", .check_certificate = true}, b;
    CHECK(subSetPassword(&a, "sesame", "c19b2d") && !subUsesApiKey(&a));
    CHECK(subSetApiKey(&a, "  nds_AbC123xyz~+/=  "));  // Spaces around it, as a keyboard leaves them.
    CHECK_STR(a.api_key, "nds_AbC123xyz~+/=");
    CHECK(subUsesApiKey(&a) && subSignedIn(&a));
    CHECK(!a.user[0] && !a.salt[0] && !a.token[0]);  // One way of signing in at a time.
    char url[1024];
    CHECK(subRequestUrl(&a, "ping", NULL, url, sizeof(url)));
    CHECK_STR(url, "http://pc:4533/rest/ping?apiKey=nds_AbC123xyz~%2B%2F%3D&v=1.16.1&c=KaiPlayer&f=json");
    snprintf(a.user, sizeof(a.user), "%s", "kai");  // The name tokenInfo gave: shown, never sent.
    CHECK(subRequestUrl(&a, "stream", "id=1&format=raw", url, sizeof(url)));
    CHECK(!strstr(url, "u=") && !strstr(url, "&t=") && strstr(url, "&f=json&id=1&format=raw"));
    CHECK(!subRequestUrl(&a, "ping", NULL, url, 40));

    char text[1024];
    CHECK(subAccountWrite(&a, text, sizeof(text)));
    CHECK(strstr(text, "api_key=nds_AbC123xyz~+/=\n") != NULL);
    subAccountRead(&b, text);
    CHECK_STR(b.api_key, a.api_key);
    CHECK_STR(b.user, "kai");
    CHECK(subUsesApiKey(&b) && subSignedIn(&b));
    subAccountRead(&b, "address=http://pc\napi_key=has space\n");
    CHECK(!b.api_key[0] && !subSignedIn(&b));  // Damaged: signed out, not sent.
    subAccountRead(&b, "api_key=nds_x\n");
    CHECK(!subSignedIn(&b));  // No address.

    // A password again: the key goes.
    CHECK(subSetPassword(&a, "sesame", "c19b2d") && !subUsesApiKey(&a) && !a.api_key[0]);
    // What can't be one.
    CHECK(!subSetApiKey(&a, "two words"));
    CHECK(!subSetApiKey(&a, "line\nbreak"));
    char long_key[SUB_KEY_MAX + 1];
    memset(long_key, 'k', SUB_KEY_MAX);
    long_key[SUB_KEY_MAX] = '\0';
    CHECK(!subSetApiKey(&a, long_key));
    long_key[SUB_KEY_MAX - 1] = '\0';
    CHECK(subSetApiKey(&a, long_key) && strlen(a.api_key) == SUB_KEY_MAX - 1);
    CHECK(subRequestUrl(&a, "ping", NULL, url, sizeof(url)));
    CHECK(subSetApiKey(&a, "   ") && !subUsesApiKey(&a) && !subSignedIn(&a));  // Nothing typed: signed out.

    CHECK(subPublicUrl("http://pc:4533", "getOpenSubsonicExtensions", url, sizeof(url)));
    CHECK_STR(url, "http://pc:4533/rest/getOpenSubsonicExtensions?v=1.16.1&c=KaiPlayer&f=json");
    CHECK(!subPublicUrl("", "ping", url, sizeof(url)));
    CHECK(!subPublicUrl("http://pc:4533", "ping", url, 20));
}

// getOpenSubsonicExtensions' and tokenInfo's answers, as Navidrome gives them.
static void testApiKeyAnswers(void)
{
    static const char *const WITH =
        "{\"subsonic-response\":{\"status\":\"ok\",\"openSubsonic\":true,\"openSubsonicExtensions\":["
        "{\"name\":\"formPost\",\"versions\":[1]},{\"name\":\"apiKeyAuthentication\",\"versions\":[1]}]}}";
    static const char *const WITHOUT =
        "{\"subsonic-response\":{\"status\":\"ok\",\"openSubsonicExtensions\":[{\"name\":\"songLyrics\"}]}}";
    static const char *const OLD = "{\"subsonic-response\":{\"status\":\"ok\",\"version\":\"1.16.1\"}}";
    static const char *const INFO =
        "{\"subsonic-response\":{\"status\":\"ok\",\"tokenInfo\":{\"username\":\"kaitest\"}}}";
    static const char *const BAD_KEY =
        "{\"subsonic-response\":{\"status\":\"failed\",\"error\":{\"code\":44,\"message\":\"Invalid API key\"}}}";
    const JsonValue *body;
    int code;
    char message[64], user[SUB_TEXT_MAX];
    JsonDoc *doc = jsonParse(WITH, strlen(WITH));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    CHECK(subHasExtension(body, SUB_API_KEY_EXTENSION) && !subHasExtension(body, "transcoding"));
    jsonFree(doc);
    doc = jsonParse(WITHOUT, strlen(WITHOUT));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    CHECK(!subHasExtension(body, SUB_API_KEY_EXTENSION) && subHasExtension(body, "songLyrics"));
    jsonFree(doc);
    doc = jsonParse(OLD, strlen(OLD));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    CHECK(!subHasExtension(body, SUB_API_KEY_EXTENSION) && !subTokenUser(body, user, sizeof(user)));
    jsonFree(doc);
    doc = jsonParse(INFO, strlen(INFO));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    CHECK(subTokenUser(body, user, sizeof(user)));
    CHECK_STR(user, "kaitest");
    CHECK(!subTokenUser(body, user, 4));  // Doesn't fit.
    jsonFree(doc);
    doc = jsonParse(BAD_KEY, strlen(BAD_KEY));
    CHECK(doc && !subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)) && code == 44);
    CHECK_STR(message, "Invalid API key");
    jsonFree(doc);
    CHECK(!subHasExtension(NULL, SUB_API_KEY_EXTENSION));
}

static void testLocations(void)
{
    char song[256], cover[256];
    CHECK(subSongLocation("5hz6LNCZcl0PpPQtBWyk33", "al-75kepKHUEHNN3Rzvc6icOq_f7729e5d057838c2", "FLAC", song,
                          sizeof(song)));
    CHECK_STR(song, "sub:/al-75kepKHUEHNN3Rzvc6icOq_f7729e5d057838c2/5hz6LNCZcl0PpPQtBWyk33.flac");
    CHECK(subIsLocation(song) && !subIsLocation("dav:/a.flac") && !subIsLocation(NULL));
    SubTarget t;
    CHECK(subParseLocation(song, &t) && !t.cover);
    CHECK_STR(t.id, "5hz6LNCZcl0PpPQtBWyk33");
    CHECK(subCoverLocation(song, 300, cover, sizeof(cover)));
    CHECK_STR(cover, "sub:/al-75kepKHUEHNN3Rzvc6icOq_f7729e5d057838c2/cover-300.jpg");
    CHECK(subParseLocation(cover, &t) && t.cover && t.px == 300);
    CHECK_STR(t.id, "al-75kepKHUEHNN3Rzvc6icOq_f7729e5d057838c2");

    CHECK(subSongLocation("tr/1 ?", "", "mp3", song, sizeof(song)));  // Encoded: one '/' only.
    CHECK_STR(song, "sub:/-/tr%2F1%20%3F.mp3");
    CHECK(subParseLocation(song, &t) && !t.cover);
    CHECK_STR(t.id, "tr%2F1%20%3F");  // As the URL takes it.
    CHECK(!subCoverLocation(song, 300, cover, sizeof(cover)));  // No cover.
    CHECK(!subParseLocation("sub:/-/cover-300.jpg", &t));
    // A playlist line can't add to the query the id goes into.
    CHECK(!subParseLocation("sub:/0/x&time=1&u=other.mp3", &t));
    CHECK(!subParseLocation("sub:/0/x#y.mp3", &t) && !subParseLocation("sub:/0/x%zz.mp3", &t));
    CHECK(!subParseLocation("sub:/a&b=c/cover-300.jpg", &t));
    CHECK(!subSongLocation("", "c", "mp3", song, sizeof(song)));
    CHECK(!subSongLocation("id", "c", "", song, sizeof(song)));
    CHECK(!subSongLocation("id", "c", "mp3/../x", song, sizeof(song)));
    CHECK(!subParseLocation("sub:/", &t) && !subParseLocation("sub:/a/b/c.flac", &t) &&
          !subParseLocation("sub:/a/noextension", &t) && !subParseLocation("dav:/a/b.flac", &t));
    CHECK(!subParseLocation("sub:/c/cover-0.jpg", &t));
    CHECK(!subCoverLocation("sub:/c/a.flac", 0, cover, sizeof(cover)));
}

// What Navidrome answers to search3 (shortened): a song with a cover, one without, a video, one KaiPlayer can't
// play, and a single song as an object (old servers).
static const char SEARCH[] =
    "{\"subsonic-response\":{\"status\":\"ok\",\"version\":\"1.16.1\",\"type\":\"navidrome\",\"openSubsonic\":true,"
    "\"searchResult3\":{\"song\":["
    "{\"id\":\"5hz6\",\"parent\":\"p\",\"isDir\":false,\"title\":\"Morning Light\",\"album\":\"Paper Boats\","
    "\"artist\":\"Aurora Fields feat. Ñu\",\"track\":1,\"discNumber\":2,\"year\":2024,\"genre\":\"Ambient\","
    "\"coverArt\":\"al-75ke_f772\",\"size\":1879056,\"suffix\":\"flac\",\"duration\":40,"
    "\"replayGain\":{\"trackGain\":-6.5},\"displayAlbumArtist\":\"Aurora Fields\","
    "\"albumArtists\":[{\"id\":\"1\",\"name\":\"Wrong\"}]},"
    "{\"id\":\"73R2\",\"title\":\"\",\"album\":\"\",\"artist\":\"\",\"size\":641127,\"suffix\":\"\","
    "\"path\":\"Aurora Fields/Neon Harbor/01 - Neon Harbor.mp3\",\"albumArtists\":[{\"name\":\"Aurora Fields\"}]},"
    "{\"id\":\"v1\",\"title\":\"A video\",\"isVideo\":true,\"suffix\":\"mp4\"},"
    "{\"id\":\"w1\",\"title\":\"Opus\",\"suffix\":\"opus\"},"
    "{\"id\":42,\"title\":\"Numbered\",\"suffix\":\"ogg\",\"album\":\"N\"}"
    "]}}}";

// A song whose genre names books is an audiobook's (Audiobooks has it, booksub.c): not one of the songs.
static const char BOOK_SONGS[] =
    "{\"subsonic-response\":{\"status\":\"ok\",\"version\":\"1.16.1\",\"searchResult3\":{\"song\":["
    "{\"id\":\"s1\",\"title\":\"Tune\",\"album\":\"A\",\"genre\":\"Pop\",\"suffix\":\"mp3\"},"
    "{\"id\":\"b1\",\"title\":\"Chapter 1\",\"album\":\"Saga\",\"genre\":\"Audiobook\",\"suffix\":\"mp3\"},"
    "{\"id\":\"b2\",\"title\":\"Kapitel 1\",\"album\":\"Sage\",\"genre\":\"H\xc3\xb6rbuch\",\"suffix\":\"mp3\"}"
    "]}}}";

static void testBookSongs(void)
{
    JsonDoc *doc = jsonParse(BOOK_SONGS, strlen(BOOK_SONGS));
    const JsonValue *body;
    int code;
    char message[128];
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    Catalog c;
    catalogInit(&c);
    size_t added = 0;
    CHECK(subAddSongs(&c, jsonGet(body, "searchResult3"), "song", NULL, &added));
    CHECK(added == 1 && c.song_count == 1 && c.book_count == 0);
    CHECK(catalogBuild(&c) && catalogFind(&c, "sub:/-/s1.mp3") >= 0);
    catalogFree(&c);
    jsonFree(doc);
}

static void testSongs(void)
{
    JsonDoc *doc = jsonParse(SEARCH, strlen(SEARCH));
    const JsonValue *body;
    int code;
    char message[128];
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    Catalog c;
    catalogInit(&c);
    size_t added = 0;
    CHECK(subAddSongs(&c, jsonGet(body, "searchResult3"), "song", NULL, &added));
    CHECK(added == 4 && c.song_count == 4);  // Not the video; the Opus song plays (FFmpeg).
    CHECK(catalogBuild(&c));
    CHECK(catalogFind(&c, "sub:/-/w1.opus") >= 0);
    long i = catalogFind(&c, "sub:/al-75ke_f772/5hz6.flac");
    CHECK(i >= 0);
    if (i >= 0) {
        const CatalogSong *s = &c.songs[i];
        CHECK_STR(s->title, "Morning Light");
        CHECK_STR(s->artist, "Aurora Fields feat. Ñu");
        CHECK_STR(s->album_artist, "Aurora Fields");  // OpenSubsonic's, before the list.
        CHECK_STR(s->album, "Paper Boats");
        CHECK(s->track == 1 && s->disc == 2 && s->year == 2024 && s->size == 1879056);
    }
    i = catalogFind(&c, "sub:/-/73R2.mp3");  // The suffix from its path; no cover.
    CHECK(i >= 0);
    if (i >= 0) {
        CHECK_STR(c.songs[i].title, "01 - Neon Harbor");
        CHECK_STR(c.songs[i].album, "Unknown album");
        CHECK_STR(c.songs[i].album_artist, "Aurora Fields");
    }
    CHECK(catalogFind(&c, "sub:/-/42.ogg") >= 0);
    catalogFree(&c);
    jsonFree(doc);

    // getAlbum's songs, with no album artist of their own, and a single song as an object.
    static const char ALBUM[] = "{\"subsonic-response\":{\"status\":\"ok\",\"album\":{\"id\":\"a\",\"artist\":\"Band\","
                                "\"song\":{\"id\":\"s1\",\"title\":\"Only\",\"artist\":\"Guest\",\"suffix\":\"mp3\"}}}}";
    doc = jsonParse(ALBUM, strlen(ALBUM));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    catalogInit(&c);
    added = 0;
    const JsonValue *album = jsonGet(body, "album");
    CHECK(subAddSongs(&c, album, "song", jsonText(album, "artist"), &added) && added == 1);
    CHECK(catalogBuild(&c) && c.song_count == 1 && !strcmp(c.songs[0].album_artist, "Band") &&
          !strcmp(c.songs[0].artist, "Guest"));
    catalogFree(&c);
    jsonFree(doc);

    static const char FAILED[] = "{\"subsonic-response\":{\"status\":\"failed\",\"version\":\"1.16.1\","
                                 "\"error\":{\"code\":40,\"message\":\"Wrong username or password\"}}}";
    doc = jsonParse(FAILED, strlen(FAILED));
    CHECK(doc && !subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)) && code == 40);
    CHECK_STR(message, "Wrong username or password");
    jsonFree(doc);
    doc = jsonParse("{\"other\":1}", 11);  // JSON, but not Subsonic's.
    CHECK(doc && !subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)) && code == 0);
    jsonFree(doc);

    static const char SCAN[] = "{\"subsonic-response\":{\"status\":\"ok\",\"scanStatus\":{\"scanning\":false,\"count\":1602}}}";
    doc = jsonParse(SCAN, strlen(SCAN));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)) && subScanCount(body) == 1602);
    jsonFree(doc);
    CHECK(subScanCount(NULL) == -1);
}

// getLyricsBySongId (OpenSubsonic): synced lyrics first, as LRC; plain ones when there are no others.
static void testServerLyrics(void)
{
    static const char BOTH[] =
        "{\"subsonic-response\":{\"status\":\"ok\",\"lyricsList\":{\"structuredLyrics\":["
        "{\"lang\":\"eng\",\"synced\":false,\"line\":[{\"value\":\"plain one\"},{\"value\":\"plain two\"}]},"
        "{\"lang\":\"eng\",\"synced\":true,\"offset\":-100,\"line\":[{\"start\":1500,\"value\":\"first\"},"
        "{\"start\":61230,\"value\":\"second\"},{\"start\":50,\"value\":\"\"}]}]}}}";
    const JsonValue *body;
    int code;
    char message[64], out[256];
    JsonDoc *doc = jsonParse(BOTH, strlen(BOTH));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    CHECK(subLyricsText(body, out, sizeof(out)));
    CHECK_STR(out, "[00:01.40]first\n[01:01.13]second\n[00:00.00]\n");
    CHECK(!subLyricsText(body, out, 20));  // Doesn't fit.
    jsonFree(doc);

    static const char PLAIN[] = "{\"subsonic-response\":{\"status\":\"ok\",\"lyricsList\":{\"structuredLyrics\":"
                                "{\"synced\":false,\"line\":{\"value\":\"only line\"}}}}}";
    doc = jsonParse(PLAIN, strlen(PLAIN));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    CHECK(subLyricsText(body, out, sizeof(out)));
    CHECK_STR(out, "only line\n");
    jsonFree(doc);

    static const char NONE[] = "{\"subsonic-response\":{\"status\":\"ok\",\"lyricsList\":{}}}";
    doc = jsonParse(NONE, strlen(NONE));
    CHECK(doc && subAnswer(jsonRoot(doc), &body, &code, message, sizeof(message)));
    CHECK(!subLyricsText(body, out, sizeof(out)) && !out[0]);
    jsonFree(doc);
    CHECK(!subLyricsText(NULL, out, sizeof(out)));
}

void testSubsonic(void)
{
    testServerLyrics();
    testMd5();
    testJson();
    testAddresses();
    testAccount();
    testApiKey();
    testApiKeyAnswers();
    testLocations();
    testSongs();
    testBookSongs();
}
