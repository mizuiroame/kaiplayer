// The Home tab: rows of artists, albums and playlists to jump back into,
// and the play history behind them.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "app_buttons.h"
#include "catalog_scan.h"
#include "player.h"

#define HISTORY_DIR "sdmc:/config/kaiplayer"
#define HISTORY_PATH HISTORY_DIR "/history.txt"
#define PLAY_SECONDS 30.0  // A song counts as played after this long (or half of a short one).
#define RESTART_SECONDS 3.0

void appLoadHistory(App *app)
{
    historyInit(&app->history);
    FILE *f = fopen(HISTORY_PATH, "r");
    if (!f)
        return;
    historyLoad(&app->history, f);
    fclose(f);
    if (app->history.count < HISTORY_MAX)
        return;
    f = fopen(HISTORY_PATH, "w");  // Full: rewrite without the dropped lines.
    if (f) {
        historySave(&app->history, f);
        fclose(f);
    }
}

void appRecordPlay(App *app, HistoryKind kind, const char *key)
{
    if (!historyAdd(&app->history, kind, key))
        return;
    mkdir("sdmc:/config", 0777);
    mkdir(HISTORY_DIR, 0777);
    FILE *f = fopen(HISTORY_PATH, "a");
    if (f) {
        historyWriteEntry(f, kind, key);
        fclose(f);
    }
}

// How long a song plays before it counts: 30 s, or half of a short song.
static double needed(const PlayerStatus *status)
{
    return status->duration > 0 && status->duration < 2 * PLAY_SECONDS ? status->duration / 2 : PLAY_SECONDS;
}

void appTrackPlays(App *app, const PlayerStatus *status)
{
    HomeTracker *t = &app->home_tracker;
    if (status->state == PLAYER_STOPPED || status->track_count == 0)
        return;
    if (status->generation != t->generation || status->track != t->track) {  // Another song.
        t->generation = status->generation;
        t->track = status->track;
        t->armed = true;
    } else if (!t->armed && status->position < RESTART_SECONDS && status->position < needed(status)) {
        t->armed = true;  // The same song from its start again (repeat song).
    }
    if (!t->armed || status->position < needed(status))
        return;
    t->armed = false;
    char path[UTIL_PATH_MAX];
    if (playerTrackPath(status->track, path, sizeof(path)))
        appRecordPlay(app, HISTORY_SONG, path);
}

// The next row with items from row, going by step (+1 or -1); -1 if none.
static int nextRow(const Home *home, int row, int step)
{
    for (int r = row + step; r >= 0 && r < HOME_ROW_COUNT; r += step)
        if (homeRowCount(home, (HomeRowKind)r) > 0)
            return r;
    return -1;
}

static void openLibraryPage(App *app, LibraryLevel list, size_t list_row, LibraryLevel page, size_t arg)
{
    LibraryTab *t = &app->library;
    t->stack[0] = (LibraryPage){.level = LIB_ROOT, .cursor = list == LIB_ARTISTS ? 0 : 1};
    t->stack[1] = (LibraryPage){.level = list, .cursor = list_row};
    t->stack[2] = (LibraryPage){.level = page, .arg = arg};
    t->depth = 3;
    t->generation = app->home_generation;
    app->focus = FOCUS_LIST;
    appSetView(app, VIEW_BROWSER);
}

// Opens (or plays, or shuffles) the selected card.
static void actOnCard(App *app, const Home *home, bool play, bool shuffle)
{
    HomeRowKind row = (HomeRowKind)app->home_row;
    size_t col = app->home_col[row];
    if (col >= homeRowCount(home, row))
        return;
    if (row == HOME_RECENT_PLAYLISTS) {
        appPlaylistsRefresh(app);
        appPlaylistsSelect(app, home->playlists[col]);
        if (play)
            appPlaylistsPlaySelected(app, shuffle);
        else {
            appSetView(app, VIEW_PLAYLISTS);
            appPlaylistsOpenSelected(app);
        }
        return;
    }
    size_t item = home->rows[row].items[col];
    bool artist = row != HOME_RECENT_ALBUMS;
    // Opening or playing goes through the Library page, which knows how.
    openLibraryPage(app, artist ? LIB_ARTISTS : LIB_ALBUMS, item, artist ? LIB_ARTIST : LIB_ALBUM, item);
    if (play) {
        appSetView(app, VIEW_HOME);  // Play from here, without leaving Home.
        appLibraryPlayPage(app, shuffle);
    }
}

void appHandleHome(App *app, uint64_t pressed)
{
    if (app->focus == FOCUS_PLAYER) {  // The player bar under Home.
        appFocusFilter(app, pressed, 0, 0, false, false);
        return;
    }
    const Catalog *c = catalogLock();
    const Home *home = appHome(app, c);
    catalogUnlock();
    if (homeRowCount(home, (HomeRowKind)app->home_row) == 0) {
        int first = nextRow(home, -1, 1);
        app->home_row = first < 0 ? 0 : first;
    }
    HomeRowKind row = (HomeRowKind)app->home_row;
    size_t count = homeRowCount(home, row);
    if ((pressed & BUTTONS_LEFT) && app->home_col[row] > 0)
        app->home_col[row]--;
    if ((pressed & BUTTONS_RIGHT) && app->home_col[row] + 1 < count)
        app->home_col[row]++;
    if (pressed & BUTTONS_UP) {
        int up = nextRow(home, app->home_row, -1);
        app->home_row = up < 0 ? app->home_row : up;
    }
    if (pressed & BUTTONS_DOWN) {
        int down = nextRow(home, app->home_row, 1);
        if (down >= 0) {
            app->home_row = down;
        } else {  // Below the last row: the player bar.
            app->focus = FOCUS_PLAYER;
            app->bar_control = BAR_PLAY;
            return;
        }
    }
    if (count == 0)
        return;
    if (pressed & HidNpadButton_A)
        actOnCard(app, home, false, false);
    else if (pressed & HidNpadButton_X)
        actOnCard(app, home, true, false);
    else if (pressed & HidNpadButton_Y)
        actOnCard(app, home, true, true);
}
