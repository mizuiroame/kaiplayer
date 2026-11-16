// Finds the tag data in a file and hands it to the memory parsers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codecs_config.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "tags.h"
#include "tags_text.h"
#include "util.h"

#define ID3_HEADER_SIZE 10
#define ID3V1_SIZE 128
#define FLAC_MAX_BLOCKS 128
#define FLAC_BLOCK_COMMENT 4
#define FLAC_BLOCK_PICTURE 6
#define FLAC_PICTURE_BLOCK_MAX (TAGS_PICTURE_MAX + (64u << 10))

void tagsInit(TrackTags *t)
{
    memset(t, 0, sizeof(*t));
    t->picture_type = -1;
}

void tagsFree(TrackTags *t)
{
    free(t->picture);
    tagsInit(t);
}

// Reads size bytes into a new buffer. NULL on a short read.
#define ID3_TEXT_ONLY_MAX (256 * 1024)

static uint8_t *readBlock(FILE *f, size_t size)
{
    uint8_t *buf = malloc(size ? size : 1);
    if (buf && fread(buf, 1, size, f) != size) {
        free(buf);
        return NULL;
    }
    return buf;
}

// Reads a whole ID3v2 tag at the current position, if there is one.
// Leaves the file after the tag either way.
static void readId3v2(FILE *f, TrackTags *t)
{
    long start = ftell(f);
    uint8_t header[ID3_HEADER_SIZE];
    if (fread(header, 1, sizeof(header), f) != sizeof(header) || memcmp(header, "ID3", 3) != 0) {
        fseek(f, start, SEEK_SET);
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
            if (fread(tag + ID3_HEADER_SIZE, 1, wanted, f) == wanted)
                tagsParseId3v2(tag, ID3_HEADER_SIZE + wanted, t);
            free(tag);
        }
    }
    fseek(f, start + (long)total, SEEK_SET);
}

static void readMp3(FILE *f, TrackTags *t)
{
    readId3v2(f, t);
    uint8_t tail[ID3V1_SIZE];
    if (fseek(f, -(long)ID3V1_SIZE, SEEK_END) == 0 && fread(tail, 1, sizeof(tail), f) == sizeof(tail))
        tagsParseId3v1(tail, sizeof(tail), t);
}

static void readFlacBlock(FILE *f, int type, size_t len, TrackTags *t)
{
    bool wanted = (type == FLAC_BLOCK_COMMENT && len <= TAGS_COMMENT_BLOCK_MAX) ||
                  (type == FLAC_BLOCK_PICTURE && len <= FLAC_PICTURE_BLOCK_MAX && !t->skip_picture);
    if (!wanted) {
        fseek(f, (long)len, SEEK_CUR);
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

static void readFlac(FILE *f, TrackTags *t)
{
    readId3v2(f, t);  // Some taggers put ID3 in front of FLAC.
    uint8_t magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "fLaC", 4) != 0)
        return;
    for (int i = 0; i < FLAC_MAX_BLOCKS; i++) {
        uint8_t header[4];
        if (fread(header, 1, 4, f) != 4)
            return;
        bool last = header[0] & 0x80;
        readFlacBlock(f, header[0] & 0x7F, readBe24(header + 1), t);
        if (last)
            return;
    }
}

static void readOgg(const char *path, TrackTags *t)
{
    int error = 0;
    stb_vorbis *v = stb_vorbis_open_filename(path, &error, NULL);
    if (!v)
        return;
    stb_vorbis_comment comments = stb_vorbis_get_comment(v);
    for (int i = 0; i < comments.comment_list_length; i++) {
        if (comments.comment_list[i])
            tagsApplyComment(comments.comment_list[i], strlen(comments.comment_list[i]), t);
    }
    stb_vorbis_close(v);
}

void tagsRead(const char *path, TrackTags *t)
{
    if (utilHasExtension(path, "ogg") || utilHasExtension(path, "oga")) {
        readOgg(path, t);
        return;
    }
    bool mp3 = utilHasExtension(path, "mp3");
    if (!mp3 && !utilHasExtension(path, "flac"))
        return;
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    if (mp3)
        readMp3(f, t);
    else
        readFlac(f, t);
    fclose(f);
}
