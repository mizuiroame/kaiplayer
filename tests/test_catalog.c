// The library catalog: grouping, order, folder fallbacks and the cache file.
#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "check.h"
#include "util.h"

static void addTagged(Catalog *c, const char *path, const char *title, const char *artist, const char *album,
                      int track, int year)
{
    TrackTags t;
    tagsInit(&t);
    snprintf(t.title, sizeof(t.title), "%s", title);
    snprintf(t.artist, sizeof(t.artist), "%s", artist);
    snprintf(t.album, sizeof(t.album), "%s", album);
    t.track_number = track;
    t.year = year;
    CHECK(catalogAdd(c, path, 100, &t, "sdmc:/music"));
    tagsFree(&t);
}

static void addUntagged(Catalog *c, const char *path)
{
    TrackTags t;
    tagsInit(&t);
    CHECK(catalogAdd(c, path, 50, &t, "sdmc:/music"));
    tagsFree(&t);
}

static void fill(Catalog *c)
{
    addTagged(c, "sdmc:/music/b/2.flac", "Second", "Band", "Later", 2, 2020);
    addTagged(c, "sdmc:/music/b/1.flac", "First", "Band", "Later", 1, 2020);
    addTagged(c, "sdmc:/music/a/x.mp3", "Early One", "band", "Early", 1, 2001);  // Same artist, other case.
    addTagged(c, "sdmc:/music/z/solo.ogg", "Alone", "Zed", "Solo", 0, 0);
    addUntagged(c, "sdmc:/music/Folder Artist/Folder Album/03 Song.mp3");
    addUntagged(c, "sdmc:/music/loose.wav");
}

static void testGrouping(void)
{
    Catalog c;
    catalogInit(&c);
    fill(&c);
    CHECK(catalogBuild(&c));
    CHECK(c.song_count == 6 && c.album_count == 5 && c.artist_count == 4);

    // Artists by name: Band, Folder Artist, Unknown artist, Zed.
    CHECK(utilCompareNames(c.artists[0].name, "Band") == 0 && c.artists[0].album_count == 2 && c.artists[0].song_count == 3);
    CHECK(strcmp(c.artists[1].name, "Folder Artist") == 0);
    CHECK(strcmp(c.artists[2].name, CATALOG_UNKNOWN_ARTIST) == 0);
    // An artist's albums go by year: Early (2001), then Later (2020).
    const CatalogAlbum *early = &c.albums[c.artists[0].albums[0]], *later = &c.albums[c.artists[0].albums[1]];
    CHECK(strcmp(early->title, "Early") == 0 && strcmp(later->title, "Later") == 0 && later->year == 2020);
    // An album's songs go by track number.
    CHECK(later->song_count == 2 && strcmp(c.songs[later->songs[0]].title, "First") == 0);

    // No tags: names from the folders and the file.
    long untagged = catalogFind(&c, "sdmc:/music/Folder Artist/Folder Album/03 Song.mp3");
    CHECK(untagged >= 0 && strcmp(c.songs[untagged].title, "03 Song") == 0);
    CHECK(strcmp(c.albums[c.songs[untagged].album_index].title, "Folder Album") == 0);
    long loose = catalogFind(&c, "sdmc:/music/loose.wav");
    CHECK(loose >= 0 && strcmp(c.songs[loose].album, "Unknown album") == 0);
    CHECK(catalogFind(&c, "sdmc:/music/missing.mp3") == -1);

    // "Songs" goes by title.
    CHECK(strcmp(c.songs[c.songs_by_title[0]].title, "03 Song") == 0);
    CHECK(strcmp(c.songs[c.songs_by_title[c.song_count - 1]].title, "Second") == 0);
    catalogFree(&c);
}

static void testCache(void)
{
    Catalog c, back;
    catalogInit(&c);
    catalogInit(&back);
    fill(&c);
    addTagged(&c, "sdmc:/music/t/tab.mp3", "Tab\tin title", "Line\nbreak", "A", 1, 1999);
    CHECK(catalogBuild(&c));
    FILE *f = tmpfile();
    CHECK(f && catalogSave(&c, f));
    rewind(f);
    fputs("", f);
    CHECK(catalogLoad(&back, f) && catalogBuild(&back));
    fclose(f);
    CHECK(back.song_count == c.song_count && back.album_count == c.album_count);
    long tab = catalogFind(&back, "sdmc:/music/t/tab.mp3");
    CHECK(tab >= 0 && strcmp(back.songs[tab].title, "Tab in title") == 0 && back.songs[tab].year == 1999);
    catalogFree(&c);
    catalogFree(&back);

    // Not a cache file: nothing loaded.
    FILE *junk = tmpfile();
    fputs("hello\n", junk);
    rewind(junk);
    catalogInit(&back);
    CHECK(!catalogLoad(&back, junk) && back.song_count == 0);
    fclose(junk);
    catalogFree(&back);
}

// A network song keeps its time; KaiPlayer 1.1's files (without times) still load.
static void testCacheTimes(void)
{
    Catalog c, back;
    catalogInit(&c);
    catalogInit(&back);
    TrackTags t;
    tagsInit(&t);
    snprintf(t.title, sizeof(t.title), "%s", "Far");
    CHECK(catalogAddFile(&c, "dav:/Band/Album/far.flac", 1234567, 1795293630, &t, "dav:/"));
    CHECK(catalogAdd(&c, "sdmc:/music/near.flac", 99, &t, "sdmc:/music"));
    CHECK(catalogBuild(&c));
    FILE *f = tmpfile();
    CHECK(f && catalogSave(&c, f));
    rewind(f);
    CHECK(catalogLoad(&back, f) && catalogBuild(&back));
    fclose(f);
    long far = catalogFind(&back, "dav:/Band/Album/far.flac"), near = catalogFind(&back, "sdmc:/music/near.flac");
    CHECK(far >= 0 && back.songs[far].modified == 1795293630 && back.songs[far].size == 1234567);
    CHECK(far >= 0 && strcmp(back.songs[far].album, "Album") == 0 && strcmp(back.songs[far].artist, "Band") == 0);
    CHECK(near >= 0 && back.songs[near].modified == 0 && back.songs[near].size == 99);
    catalogFree(&c);
    catalogFree(&back);

    FILE *old = tmpfile();
    fputs("KAIPLAYER-CATALOG 1\n100\t3\t1\t2001\tsdmc:/music/a.mp3\tA\tArtist\tArtist\tAlbum\n", old);
    rewind(old);
    catalogInit(&back);
    CHECK(catalogLoad(&back, old) && catalogBuild(&back) && back.song_count == 1);
    CHECK(back.song_count == 1 && back.songs[0].size == 100 && back.songs[0].track == 3 && back.songs[0].year == 2001 &&
          back.songs[0].modified == 0 && strcmp(back.songs[0].title, "A") == 0 && strcmp(back.songs[0].album, "Album") == 0);
    fclose(old);
    catalogFree(&back);
    tagsFree(&t);
}

static void testNumbers(void)
{
    CHECK(tagsNumber("3/12") == 3 && tagsNumber(" 2019-05-01") == 2019 && tagsNumber("abc") == 0);
    CHECK(tagsNumber("12345678") == 123456);  // Bounded.
}

void testCatalog(void)
{
    testGrouping();
    testCache();
    testCacheTimes();
    testNumbers();
}
