#include "replaygain.h"

#include <math.h>

float replayGainDb(ReplayGainMode mode, bool shuffle, bool mixed_albums, const TrackTags *tags)
{
    if (mode == REPLAYGAIN_OFF)
        return 0.0f;
    bool album = mode == REPLAYGAIN_ALBUM || (mode == REPLAYGAIN_AUTO && !shuffle && !mixed_albums);
    if (album && tags->has_album_gain)
        return tags->album_gain_db;
    return tags->has_track_gain ? tags->track_gain_db : 0.0f;
}

float replayGainFactor(float db)
{
    return db < 0.0f ? powf(10.0f, db / 20.0f) : 1.0f;
}
