// The Playlists tab: the list of playlists, or the songs of an open one.
#include <stdio.h>
#include <string.h>

#include "lang.h"
#include "ui.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "ui_list.h"

static void drawPlaylistIcon(int x, int y, Color color)
{
    for (int i = 0; i < 3; i++)
        gfxFillRoundRect(x, y + i * 6, 14, 3, 1, color);
    gfxFillCircle(x + 19, y + 15, 4, color);
    gfxFillRect(x + 22, y + 1, 2, 15, color);
}

static void drawSongs(const PlaylistsTab *t, const PlayerStatus *status, Focus focus, uint64_t now_ns)
{
    char line[384];
    if (t->tracks.count == 0) {
        snprintf(line, sizeof(line), "%s  ·  %s", t->name, T("empty"));
        int back_w = uiDrawBackButton(MARGIN - 4, SUBTITLE_Y - 6, focus == FOCUS_BACK) + 12;
        fontDraw(FONT_BOLD, TEXT_SMALL, MARGIN + back_w, SUBTITLE_Y, line, COLOR_DIM);
        fontDraw(FONT_REGULAR, TEXT_ROW, MARGIN, LIST_Y + 10,
                 T("Add songs with the + buttons in Library, Queue or Now Playing."), COLOR_DIM);
        return;
    }
    char songs[64];
    snprintf(songs, sizeof(songs), t->tracks.count == 1 ? T("%zu song") : T("%zu songs"), t->tracks.count);
    snprintf(line, sizeof(line), "%s  ·  %s%s%s", t->name, songs, t->moving ? "  ·  " : "",
             t->moving ? T("moving a song") : "");
    int back_w = uiDrawBackButton(MARGIN - 4, SUBTITLE_Y - 6, focus == FOCUS_BACK) + 12;
    fontDrawClipped(FONT_BOLD, TEXT_SMALL, MARGIN + back_w, SUBTITLE_Y, COLUMN_ARTIST_X - 24 - MARGIN - back_w, line,
                    t->moving ? COLOR_FOLDER : COLOR_DIM);
    uiDrawColumnHeaders();

    char playing[UTIL_PATH_MAX] = "";
    if (status->state != PLAYER_STOPPED)
        playerTrackPath(status->track, playing, sizeof(playing));
    int offset;
    size_t first = uiListBegin(&t->track_cursor, t->track_cursor, t->tracks.count, &offset);
    uiHitListArea();
    for (size_t i = first; i < t->tracks.count && i <= first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H - offset;
        uiHitRow(y, i);
        bool selected = i == t->track_cursor;
        bool current = playing[0] && strcmp(playing, t->tracks.paths[i]) == 0;
        if (selected && t->moving)
            uiDrawSelection(y, COLOR_FOLDER);  // A different bar while moving.
        TrackRow row = {
            .number = i + 1, .path = t->tracks.paths[i], .y = y, .index = i,
            .selected = selected && !t->moving, .current = current,
            .playing = status->state == PLAYER_PLAYING, .add_focused = selected && focus == FOCUS_ADD,
            .remove_button = true,
            .now_ns = now_ns,
        };
        uiDrawTrackRow(&row);
    }
    uiListEnd();
    uiDrawScrollbar(first, t->tracks.count);
}

static void drawList(const PlaylistsTab *t)
{
    if (t->index.count == 0) {
        uiDrawEmptyList(T("No playlists yet"),
                        T("Press + to create one, or to add songs from Library, Queue or Now Playing."));
        return;
    }
    char count[64], line[160];
    snprintf(count, sizeof(count), t->index.count == 1 ? T("%zu playlist") : T("%zu playlists"), t->index.count);
    snprintf(line, sizeof(line), "%s  ·  %.40s", count, playlistRoot());
    fontDraw(FONT_REGULAR, TEXT_SMALL, MARGIN, SUBTITLE_Y, line, COLOR_DIM);

    int offset;
    size_t first = uiListBegin(&t->cursor, t->cursor, t->index.count, &offset);
    uiHitListArea();
    for (size_t i = first; i < t->index.count && i <= first + UI_LIST_ROWS; i++) {
        int y = LIST_Y + (int)(i - first) * ROW_H - offset;
        uiHitRow(y, i);
        bool selected = i == t->cursor;
        if (selected)
            uiDrawSelection(y, COLOR_ACCENT);
        drawPlaylistIcon(MARGIN + 4, y + 13, COLOR_ACCENT);
        fontDrawClipped(selected ? FONT_BOLD : FONT_REGULAR, TEXT_ROW, MARGIN + 44, y + 8,
                        GFX_WIDTH - 2 * MARGIN - 60, t->index.names[i], COLOR_TEXT);
    }
    uiListEnd();
    uiDrawScrollbar(first, t->index.count);
}

void uiDrawPlaylists(const App *app, const PlayerStatus *status, uint64_t now_ns)
{
    if (app->playlists.open)
        drawSongs(&app->playlists, status, app->focus, now_ns);
    else
        drawList(&app->playlists);
}
