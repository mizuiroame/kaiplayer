// Reads title, artist, album and embedded cover art from audio files:
// ID3v2/ID3v1 (MP3), FLAC metadata blocks and Vorbis comments (FLAC, Ogg),
// and through FFmpeg's metadata (ffaudio.c) MP4/M4A, AAC and Opus.
// The parsers work on memory, take untrusted input and check every length.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAGS_TEXT_MAX 256
#define TAGS_PICTURE_MAX (8u << 20)
#define TAGS_ID3_MAX (16u << 20)
#define TAGS_COMMENT_BLOCK_MAX (1u << 20)
#define TAGS_LYRICS_MAX (256u * 1024)  // As LYRICS_FILE_MAX: more isn't lyrics.

typedef struct {
    char title[TAGS_TEXT_MAX];
    char artist[TAGS_TEXT_MAX];
    char album[TAGS_TEXT_MAX];
    char album_artist[TAGS_TEXT_MAX];
    int track_number;  // 0 if unknown ("3/12" is 3).
    int disc_number;
    int year;
    uint8_t *picture;  // JPEG or PNG bytes, or NULL.
    size_t picture_size;
    int picture_type;  // ID3/FLAC picture type; 3 is the front cover.
    bool skip_picture; // Set before reading to leave pictures out.
    bool want_lyrics;  // Set before reading to keep the song's lyrics (USLT, LYRICS).
    char *lyrics;      // Their UTF-8 text, or NULL.
    size_t lyrics_size;
    bool has_track_gain, has_album_gain;
    float track_gain_db;  // ReplayGain, from REPLAYGAIN_TRACK_GAIN.
    float album_gain_db;  // ReplayGain, from REPLAYGAIN_ALBUM_GAIN.
} TrackTags;

void tagsInit(TrackTags *t);
void tagsFree(TrackTags *t);

// Reads the tags of a file by its extension, on the card or in the network
// folder (kfile.h). Missing tags stay empty. False if the file couldn't be
// read (gone, or the network failed): what's empty may not be so.
bool tagsRead(const char *path, TrackTags *t);

// Parsers, exposed for tests. Each fills only the fields it finds.
// data starts with an ID3v2 header ("ID3").
void tagsParseId3v2(const uint8_t *data, size_t size, TrackTags *t);
// The 128-byte ID3v1 tag at the end of an MP3 ("TAG").
void tagsParseId3v1(const uint8_t *data, size_t size, TrackTags *t);
// The body of a FLAC VORBIS_COMMENT block (vendor, then the comments).
void tagsParseVorbisComments(const uint8_t *data, size_t size, TrackTags *t);
// The leading number of a tag value: "3/12" is 3, "2019-05-01" is 2019,
// anything else 0.
int tagsNumber(const char *value);

// A ReplayGain field ("REPLAYGAIN_TRACK_GAIN" or "..._ALBUM_GAIN", any case)
// with its value ("-6.54 dB"). Other names are ignored.
void tagsApplyReplayGain(const char *name, const char *value, TrackTags *t);

// One "KEY=value" comment, as Ogg Vorbis and FLAC store them.
void tagsApplyComment(const char *comment, size_t length, TrackTags *t);
// The body of a FLAC PICTURE block (also used inside Ogg comments).
void tagsParseFlacPicture(const uint8_t *data, size_t size, TrackTags *t);
