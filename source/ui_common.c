#include "ui_common.h"

#include <stdio.h>
#include <string.h>

void uiSplitTrackPath(const char *path, char *title, char *folder)
{
    snprintf(title, UTIL_PATH_MAX, "%s", utilFileName(path));
    char *dot = strrchr(title, '.');
    if (dot && dot != title)
        *dot = '\0';

    snprintf(folder, UTIL_PATH_MAX, "%s", path);
    if (utilParentPath(folder)) {
        const char *name = utilFileName(folder);
        memmove(folder, name, strlen(name) + 1);
    } else {
        folder[0] = '\0';
    }
}

void uiResolveTrack(const PlayerStatus *status, const Artwork *art, UiTrack *track)
{
    memset(track, 0, sizeof(*track));
    char path[UTIL_PATH_MAX];
    track->has_track = status->track_count > 0 && playerTrackPath(status->track, path, sizeof(path));
    if (!track->has_track) {
        snprintf(track->title, sizeof(track->title), "%s", "Nothing playing");
        return;
    }
    uiSplitTrackPath(path, track->title, track->folder);
    if (!art || strcmp(art->path, path) != 0)
        return;
    if (art->title[0])
        snprintf(track->title, sizeof(track->title), "%s", art->title);
    snprintf(track->artist, sizeof(track->artist), "%s", art->artist);
    snprintf(track->album, sizeof(track->album), "%s", art->album);
    if (art->has_cover) {
        track->cover = &art->cover;
        track->thumb = &art->thumb;
    }
}

void uiDrawNoteIcon(int x, int y, int size, Color color)
{
    // Two beamed eighth notes scaled to a size x size box.
    int head = size * 28 / 100, stem = size / 14 > 1 ? size / 14 : 2;
    int left_x = x + size * 12 / 100, right_x = x + size * 56 / 100;
    int head_y = y + size * 70 / 100, top = y + size * 14 / 100;
    gfxFillCircle(left_x + head / 2, head_y + head / 4, head / 2, color);
    gfxFillCircle(right_x + head / 2, head_y + head / 8 - size / 12, head / 2, color);
    gfxFillRect(left_x + head - stem, top + size / 14, stem, head_y - top, color);
    gfxFillRect(right_x + head - stem, top, stem, head_y - top - size / 12, color);
    gfxFillRect(left_x + head - stem, top, right_x - left_x + stem, size / 9, color);
}

void uiDrawCover(int x, int y, int size, const CoverImage *cover)
{
    if (cover && cover->pixels && cover->size == size) {
        gfxBlit(x, y, size, cover->pixels);
        return;
    }
    gfxFillRoundRect(x, y, size, size, size / 12, COLOR_SURFACE_HI);
    uiDrawNoteIcon(x + size / 4, y + size / 4, size / 2, COLOR_FAINT);
}

const char *uiRepeatLabel(RepeatMode repeat)
{
    switch (repeat) {
    case REPEAT_ALL: return "Repeat all";
    case REPEAT_ONE: return "Repeat one";
    default: return "Repeat off";
    }
}
