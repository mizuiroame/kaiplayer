// Finds the tag data in a file and hands it to the memory parsers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codecs_config.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "dsd.h"
#include "ffaudio.h"
#include "kfile.h"
#include "tags.h"
#include "tags_text.h"
#include "util.h"

#define ID3_HEADER_SIZE 10
#define ID3V1_SIZE 128
#define FLAC_MAX_BLOCKS 128
#define FLAC_BLOCK_COMMENT 4
#define FLAC_BLOCK_PICTURE 6
#define FLAC_PICTURE_BLOCK_MAX (TAGS_PICTURE_MAX + (64u << 10))
// A network Ogg file's start, for stb_vorbis (which reads memory, not
// kfile): its headers, with the comments, are in there; with a large
// picture in them, a longer start.
#define OGG_HEAD_SIZE (256u << 10)
#define OGG_HEAD_MAX (4u << 20)  // 256 KB, 1 MB, then 4 MB.

void tagsInit(TrackTags *t)
{
    memset(t, 0, sizeof(*t));
    t->picture_type = -1;
}

void tagsFree(TrackTags *t)
{
    free(t->picture);
    free(t->lyrics);
    bool want_lyrics = t->want_lyrics;
    tagsInit(t);
    t->want_lyrics = want_lyrics;
}

// Reads size bytes into a new buffer. NULL on a short read.
#define ID3_TEXT_ONLY_MAX (256 * 1024)

static uint8_t *readBlock(KFile *f, size_t size)
{
    uint8_t *buf = malloc(size ? size : 1);
    if (buf && kfileRead(f, buf, size) != size) {
        free(buf);
        return NULL;
    }
    return buf;
}

// Reads a whole ID3v2 tag at the current position, if there is one.
// Leaves the file after the tag either way.
static void readId3v2(KFile *f, TrackTags *t)
{
    int64_t start = kfileTell(f);
    uint8_t header[ID3_HEADER_SIZE];
    if (kfileRead(f, header, sizeof(header)) != sizeof(header) || memcmp(header, "ID3", 3) != 0) {
        kfileSeek(f, start, SEEK_SET);
        return;
    }
    size_t body = readSyncsafe32(header + 6);
    size_t total = ID3_HEADER_SIZE + body;
    // Without the picture, the start of the tag is enough (the parser
    // stops at the end of what it is given).
    size_t wanted = t->skip_picture && body > ID3_TEXT_ONLY_MAX ? ID3_TEXT_ONLY_MAX : body;
    if (total <= TAGS_ID3_MAX) {
        uint8_t *tag = malloc(ID3_HEADER_SIZE + wanted);
        if (tag) {
            memcpy(tag, header, ID3_HEADER_SIZE);
            if (kfileRead(f, tag + ID3_HEADER_SIZE, wanted) == wanted)
                tagsParseId3v2(tag, ID3_HEADER_SIZE + wanted, t);
            free(tag);
        }
    }
    kfileSeek(f, start + (int64_t)total, SEEK_SET);
}

static void readMp3(KFile *f, TrackTags *t)
{
    readId3v2(f, t);
    uint8_t tail[ID3V1_SIZE];
    if (kfileSeek(f, -(int64_t)ID3V1_SIZE, SEEK_END) && kfileRead(f, tail, sizeof(tail)) == sizeof(tail))
        tagsParseId3v1(tail, sizeof(tail), t);
}

static void readFlacBlock(KFile *f, int type, size_t len, TrackTags *t)
{
    bool wanted = (type == FLAC_BLOCK_COMMENT && len <= TAGS_COMMENT_BLOCK_MAX) ||
                  (type == FLAC_BLOCK_PICTURE && len <= FLAC_PICTURE_BLOCK_MAX && !t->skip_picture);
    if (!wanted) {
        kfileSeek(f, (int64_t)len, SEEK_CUR);
        return;
    }
    uint8_t *block = readBlock(f, len);
    if (!block)
        return;
    if (type == FLAC_BLOCK_COMMENT)
        tagsParseVorbisComments(block, len, t);
    else
        tagsParseFlacPicture(block, len, t);
    free(block);
}

static void readFlac(KFile *f, TrackTags *t)
{
    readId3v2(f, t);  // Some taggers put ID3 in front of FLAC.
    uint8_t magic[4];
    if (kfileRead(f, magic, 4) != 4 || memcmp(magic, "fLaC", 4) != 0)
        return;
    for (int i = 0; i < FLAC_MAX_BLOCKS; i++) {
        uint8_t header[4];
        if (kfileRead(f, header, 4) != 4)
            return;
        bool last = header[0] & 0x80;
        readFlacBlock(f, header[0] & 0x7F, readBe24(header + 1), t);
        if (last)
            return;
    }
}

static void applyComments(stb_vorbis *v, TrackTags *t)
{
    stb_vorbis_comment comments = stb_vorbis_get_comment(v);
    for (int i = 0; i < comments.comment_list_length; i++) {
        if (comments.comment_list[i])
            tagsApplyComment(comments.comment_list[i], strlen(comments.comment_list[i]), t);
    }
    stb_vorbis_close(v);
}

// The start of a network Ogg file, longer until its headers are all in it. False if it couldn't be read.
static bool readOggHead(KFile *f, TrackTags *t)
{
    int64_t total = kfileSize(f);
    uint8_t *head = NULL;
    for (size_t want = OGG_HEAD_SIZE; want <= OGG_HEAD_MAX; want *= 4) {
        size_t size = total < (int64_t)want ? (size_t)total : want;
        uint8_t *grown = realloc(head, size ? size : 1);
        if (!grown)
            break;
        head = grown;
        if (!kfileSeek(f, 0, SEEK_SET) || kfileRead(f, head, size) != size)
            break;
        int error = 0;
        stb_vorbis *v = stb_vorbis_open_memory(head, (int)size, &error, NULL);
        if (v) {
            applyComments(v, t);
            break;
        }
        if ((int64_t)size == total)
            break;  // The whole file: it isn't Ogg Vorbis.
    }
    free(head);
    return !kfileFailed(f);
}

static bool readOgg(KFile *f, TrackTags *t)
{
    if (ffaudioOggIsOpus(f))
        return ffaudioReadTags(f, t);
    if (!kfileStdio(f))
        return readOggHead(f, t);
    int error = 0;
    stb_vorbis *v = stb_vorbis_open_file(kfileStdio(f), 0, &error, NULL);
    if (v)
        applyComments(v, t);
    return true;
}

// DSF points to an ID3v2 tag at the end; some DSDIFF files have one in an "ID3 " chunk.
static void readDsd(KFile *f, TrackTags *t)
{
    DsdInfo info;
    if (dsdReadInfo(f, &info) && info.id3_offset && kfileSeek(f, (int64_t)info.id3_offset, SEEK_SET))
        readId3v2(f, t);
}

bool tagsRead(const char *path, TrackTags *t)
{
    bool ogg = utilHasExtension(path, "ogg") || utilHasExtension(path, "oga");
    bool mp3 = utilHasExtension(path, "mp3");
    bool dsd = utilHasExtension(path, "dsf") || utilHasExtension(path, "dff");
    bool ffmpeg = ffaudioIsFile(path);
    if (!ogg && !mp3 && !dsd && !ffmpeg && !utilHasExtension(path, "flac"))
        return true;  // A WAV: no tags to read.
    KFile *f = kfileOpen(path, KFILE_PEEK);
    if (!f)
        return false;
    bool ok = true;
    if (ffmpeg)
        ok = ffaudioReadTags(f, t) || !kfileFailed(f);  // Not a file it knows: no tags, as for a broken FLAC.
    else if (ogg)
        ok = readOgg(f, t);
    else if (mp3)
        readMp3(f, t);
    else if (dsd)
        readDsd(f, t);
    else
        readFlac(f, t);
    ok = ok && !kfileFailed(f);
    kfileClose(f);
    return ok;
}
