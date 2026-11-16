// Play history and the Home tab's rows.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "history.h"
#include "home.h"

static void addSong(Catalog *c, const char *path, const char *artist, const char *album)
{
    TrackTags t;
    memset(&t, 0, sizeof(t));
    snprintf(t.title, sizeof(t.title), "%s", path);
    snprintf(t.artist, sizeof(t.artist), "%s", artist);
    snprintf(t.album, sizeof(t.album), "%s", album);
    CHECK(catalogAdd(c, path, 1, &t, "sdmc:/m"));
}

static void testHistoryFile(void)
{
    History h, back;
    historyInit(&h);
    historyInit(&back);
    CHECK(historyAdd(&h, HISTORY_SONG, "sdmc:/m/a.mp3"));
    CHECK(historyAdd(&h, HISTORY_PLAYLIST, "Road Trip"));
    CHECK(!historyAdd(&h, HISTORY_SONG, "bad\tkey") && !historyAdd(&h, HISTORY_SONG, ""));
    FILE *f = tmpfile();
    CHECK(historySave(&h, f));
    fputs("junk line\nS\t\n", f);  // Damaged lines are skipped.
    rewind(f);
    CHECK(historyLoad(&back, f) && back.count == 2);
    CHECK(back.items[1].kind == HISTORY_PLAYLIST && strcmp(back.items[1].key, "Road Trip") == 0);
    fclose(f);
    historyFree(&back);

    bool all_added = true;
    for (int i = 0; i < HISTORY_MAX + 5; i++)  // The oldest drop off.
        all_added = historyAdd(&h, HISTORY_SONG, "sdmc:/m/x.mp3") && all_added;
    CHECK(all_added && h.count == HISTORY_MAX && strcmp(h.items[0].key, "sdmc:/m/x.mp3") == 0);
    historyFree(&h);
}

static void testHomeRows(void)
{
    Catalog c;
    catalogInit(&c);
    addSong(&c, "sdmc:/m/a1.mp3", "Ana", "First");
    addSong(&c, "sdmc:/m/a2.mp3", "Ana", "Second");
    addSong(&c, "sdmc:/m/b1.mp3", "Bea", "Bea Album");
    CHECK(catalogBuild(&c));

    History h;
    historyInit(&h);
    Home home;
    homeCompute(&home, &c, &h);  // Nothing played: albums from the library.
    CHECK(home.albums_from_library && homeRowCount(&home, HOME_RECENT_ALBUMS) == 3);
    CHECK(homeRowCount(&home, HOME_TOP_ARTISTS) == 0 && homeRowCount(&home, HOME_RECENT_PLAYLISTS) == 0);

    historyAdd(&h, HISTORY_SONG, "sdmc:/m/b1.mp3");
    historyAdd(&h, HISTORY_SONG, "sdmc:/m/b1.mp3");
    historyAdd(&h, HISTORY_SONG, "sdmc:/m/a1.mp3");
    historyAdd(&h, HISTORY_PLAYLIST, "Gym");
    historyAdd(&h, HISTORY_SONG, "sdmc:/m/gone.mp3");  // Not in the catalog: ignored.
    historyAdd(&h, HISTORY_PLAYLIST, "Chill");
    historyAdd(&h, HISTORY_PLAYLIST, "Gym");
    homeCompute(&home, &c, &h);

    // Bea played twice: top artist. Ana was played last: first in recent.
    const HomeRow *top = &home.rows[HOME_TOP_ARTISTS], *recent = &home.rows[HOME_RECENT_ARTISTS];
    CHECK(top->count == 2 && strcmp(c.artists[top->items[0]].name, "Bea") == 0);
    CHECK(recent->count == 2 && strcmp(c.artists[recent->items[0]].name, "Ana") == 0);
    const HomeRow *albums = &home.rows[HOME_RECENT_ALBUMS];
    CHECK(!home.albums_from_library && albums->count == 2 && strcmp(c.albums[albums->items[0]].title, "First") == 0);
    CHECK(home.playlist_count == 2 && strcmp(home.playlists[0], "Gym") == 0 && strcmp(home.playlists[1], "Chill") == 0);

    homeCompute(&home, NULL, &h);  // No catalog yet: playlists only.
    CHECK(home.playlist_count == 2 && homeRowCount(&home, HOME_RECENT_ALBUMS) == 0);
    historyFree(&h);
    catalogFree(&c);
}

void testHome(void)
{
    testHistoryFile();
    testHomeRows();
}
