// How Now Playing shows the song: its cover, or the cover on a spinning
// record, a spinning CD or a cassette whose reels turn.
#pragma once

#include "lang.h"

typedef enum {
    NOW_STYLE_COVER,
    NOW_STYLE_VINYL,
    NOW_STYLE_CD,
    NOW_STYLE_CASSETTE,
    NOW_STYLE_COUNT,
} NowStyle;

static inline const char *nowStyleName(int style)
{
    static const char *const NAMES[NOW_STYLE_COUNT] = {N_("Cover"), N_("Vinyl record"), N_("CD"), N_("Cassette")};
    return T(style >= 0 && style < NOW_STYLE_COUNT ? NAMES[style] : NAMES[0]);
}
