// Tag parser tests on hand-built ID3 and Vorbis comment data.
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "tags.h"
#include "tags_text.h"

typedef struct {
    uint8_t data[4096];
    size_t len;
} Buf;

static void put(Buf *b, const void *data, size_t len)
{
    memcpy(b->data + b->len, data, len);
    b->len += len;
}

static void putByte(Buf *b, uint8_t v)
{
    put(b, &v, 1);
}

static void putBe32(Buf *b, uint32_t v)
{
    uint8_t p[4] = {v >> 24, v >> 16, v >> 8, v};
    put(b, p, 4);
}

static void putLe32(Buf *b, uint32_t v)
{
    uint8_t p[4] = {v, v >> 8, v >> 16, v >> 24};
    put(b, p, 4);
}

static void putSyncsafe(Buf *b, uint32_t v)
{
    uint8_t p[4] = {(v >> 21) & 0x7F, (v >> 14) & 0x7F, (v >> 7) & 0x7F, v & 0x7F};
    put(b, p, 4);
}

static void putStr(Buf *b, const char *s)
{
    put(b, s, strlen(s));
}

// A v2.3 or v2.4 frame.
static void frame(Buf *tag, const char *id, int major, uint8_t flags, const Buf *body)
{
    putStr(tag, id);
    if (major == 4)
        putSyncsafe(tag, (uint32_t)body->len);
    else
        putBe32(tag, (uint32_t)body->len);
    putByte(tag, 0);
    putByte(tag, flags);
    put(tag, body->data, body->len);
}

static void wrapId3(Buf *out, int major, uint8_t flags, const Buf *frames)
{
    out->len = 0;
    putStr(out, "ID3");
    putByte(out, (uint8_t)major);
    putByte(out, 0);
    putByte(out, flags);
    putSyncsafe(out, (uint32_t)frames->len);
    put(out, frames->data, frames->len);
}

static void testId3v23(void)
{
    Buf frames = {0}, body = {0}, tag;
    put(&body, "\0Caf\xE9", 5);  // Latin-1.
    frame(&frames, "TIT2", 3, 0, &body);
    body.len = 0;
    put(&body, "\x01\xFF\xFE\xD1\x00u\x00", 7);  // UTF-16 LE with BOM: "Ñu".
    frame(&frames, "TPE1", 3, 0, &body);
    body.len = 0;
    put(&body, "\0image/png\0\0\0AAAA", 17);  // Type 0, not the front cover.
    frame(&frames, "APIC", 3, 0, &body);
    body.len = 0;
    put(&body, "\0image/jpeg\0\x03\0FRONT", 19);
    frame(&frames, "APIC", 3, 0, &body);
    wrapId3(&tag, 3, 0, &frames);

    TrackTags t;
    tagsInit(&t);
    tagsParseId3v2(tag.data, tag.len, &t);
    CHECK(strcmp(t.title, "Caf\xC3\xA9") == 0);
    CHECK(strcmp(t.artist, "\xC3\x91u") == 0);
    CHECK(t.picture && t.picture_size == 5 && memcmp(t.picture, "FRONT", 5) == 0);
    CHECK(t.picture_type == 3);
    tagsFree(&t);
}

static void testId3v24(void)
{
    Buf frames = {0}, body = {0}, tag;
    put(&body, "\x03\xE6\x97\xA5\xE6\x9C\xAC", 7);  // UTF-8.
    frame(&frames, "TIT2", 4, 0, &body);
    body.len = 0;
    put(&body, "\0\0\0\x05\x03" "Band", 9);  // Data length indicator first.
    frame(&frames, "TPE1", 4, 0x01, &body);
    body.len = 0;
    put(&body, "\x03Secret", 7);
    frame(&frames, "TALB", 4, 0x04, &body);  // Encrypted: skipped.
    wrapId3(&tag, 4, 0, &frames);

    TrackTags t;
    tagsInit(&t);
    tagsParseId3v2(tag.data, tag.len, &t);
    CHECK(strcmp(t.title, "\xE6\x97\xA5\xE6\x9C\xAC") == 0);
    CHECK(strcmp(t.artist, "Band") == 0);
    CHECK(t.album[0] == '\0');
    tagsFree(&t);
}

static void testId3v22AndUnsync(void)
{
    Buf frames = {0}, tag;
    put(&frames, "TT2\0\0\x05\0Old!", 11);
    put(&frames, "PIC\0\0\x09\0JPG\x03\0IMG", 15);
    wrapId3(&tag, 2, 0, &frames);
    TrackTags t;
    tagsInit(&t);
    tagsParseId3v2(tag.data, tag.len, &t);
    CHECK(strcmp(t.title, "Old!") == 0);
    CHECK(t.picture && t.picture_size == 3 && memcmp(t.picture, "IMG", 3) == 0);
    tagsFree(&t);

    // v2.3 tag-wide unsynchronization: FF 00 in the stream is FF in the data.
    frames.len = 0;
    put(&frames, "TIT2\0\0\0\x03\0\0\0\xFF\0X", 14);
    wrapId3(&tag, 3, 0x80, &frames);
    tagsInit(&t);
    tagsParseId3v2(tag.data, tag.len, &t);
    CHECK(strcmp(t.title, "\xC3\xBFX") == 0);
    tagsFree(&t);
}

static void testId3Malformed(void)
{
    Buf frames = {0}, tag;
    put(&frames, "TIT2\x7F\xFF\xFF\xFF\0\0\0Hi", 13);  // Frame larger than the tag.
    wrapId3(&tag, 3, 0, &frames);
    TrackTags t;
    tagsInit(&t);
    tagsParseId3v2(tag.data, tag.len, &t);
    CHECK(t.title[0] == '\0');

    tag.data[6] = 0x7F;  // Tag size larger than the buffer: clamped.
    tagsParseId3v2(tag.data, tag.len, &t);
    tagsParseId3v2(tag.data, 9, &t);  // Shorter than a header.
    tagsParseId3v2((const uint8_t *)"ID3\x09\0\0\0\0\0\x10", 10, &t);  // Unknown version.
    CHECK(t.title[0] == '\0' && !t.picture);
    tagsFree(&t);
}

static void testId3v1(void)
{
    uint8_t v1[128] = "TAG";
    memcpy(v1 + 3, "Old Song   ", 11);
    memcpy(v1 + 33, "Ni\xF1o", 4);
    TrackTags t;
    tagsInit(&t);
    tagsParseId3v1(v1, sizeof(v1), &t);
    CHECK(strcmp(t.title, "Old Song") == 0);
    CHECK(strcmp(t.artist, "Ni\xC3\xB1o") == 0);
    CHECK(t.album[0] == '\0');
    tagsFree(&t);
}

static void base64(const uint8_t *in, size_t len, char *out)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < len ? in[i + 1] << 8 : 0) | (i + 2 < len ? in[i + 2] : 0);
        out[o++] = A[v >> 18 & 63];
        out[o++] = A[v >> 12 & 63];
        out[o++] = i + 1 < len ? A[v >> 6 & 63] : '=';
        out[o++] = i + 2 < len ? A[v & 63] : '=';
    }
    out[o] = '\0';
}

static void flacPicture(Buf *b, int type, const char *data)
{
    putBe32(b, (uint32_t)type);
    putBe32(b, 9);
    putStr(b, "image/png");
    putBe32(b, 0);
    for (int i = 0; i < 4; i++)
        putBe32(b, 0);
    putBe32(b, (uint32_t)strlen(data));
    putStr(b, data);
}

static void comment(Buf *block, const char *text)
{
    putLe32(block, (uint32_t)strlen(text));
    putStr(block, text);
}

static void testVorbisComments(void)
{
    Buf picture = {0}, block = {0};
    flacPicture(&picture, 3, "PNGDATA");
    char encoded[256] = "METADATA_BLOCK_PICTURE=";
    base64(picture.data, picture.len, encoded + strlen(encoded));

    putLe32(&block, 6);
    putStr(&block, "vendor");
    putLe32(&block, 5);
    comment(&block, "title=Hello");
    comment(&block, "ARTIST=Me");
    comment(&block, "Album=Al");
    comment(&block, "no equals sign");
    comment(&block, encoded);

    TrackTags t;
    tagsInit(&t);
    tagsParseVorbisComments(block.data, block.len, &t);
    CHECK(strcmp(t.title, "Hello") == 0);
    CHECK(strcmp(t.artist, "Me") == 0);
    CHECK(strcmp(t.album, "Al") == 0);
    CHECK(t.picture && t.picture_size == 7 && memcmp(t.picture, "PNGDATA", 7) == 0);

    // A count that claims more comments than the block holds stops cleanly.
    block.data[10] = 0xFF;
    tagsParseVorbisComments(block.data, block.len, &t);
    tagsFree(&t);
}

static void testFlacPictureMalformed(void)
{
    Buf picture = {0};
    flacPicture(&picture, 3, "DATA");
    TrackTags t;
    tagsInit(&t);
    tagsParseFlacPicture(picture.data, picture.len - 1, &t);  // Data cut short.
    CHECK(!t.picture);
    picture.data[7] = 0xFF;  // MIME length past the end.
    tagsParseFlacPicture(picture.data, picture.len, &t);
    CHECK(!t.picture);
    tagsFree(&t);
}

static void testText(void)
{
    char out[16];
    textToUtf8((const uint8_t *)"\xD8\x3D\xDE\x00", 4, TEXT_UTF16_BE, out, sizeof(out));
    CHECK(strcmp(out, "\xF0\x9F\x98\x80") == 0);  // Surrogate pair.
    textToUtf8((const uint8_t *)"\xDE\x00", 2, TEXT_UTF16_BE, out, sizeof(out));
    CHECK(strcmp(out, "?") == 0);  // Lone low surrogate.
    textToUtf8((const uint8_t *)"abcdefghijklmnopqrstuvwxyz", 26, TEXT_LATIN1, out, sizeof(out));
    CHECK(strlen(out) == sizeof(out) - 1);  // Truncated, still terminated.
    char tiny[3];
    textToUtf8((const uint8_t *)"a\xC3\xA9", 3, TEXT_UTF8, tiny, sizeof(tiny));
    CHECK(strcmp(tiny, "a") == 0);  // A cut "é" is dropped, not half-copied.
    textToUtf8((const uint8_t *)"x", 1, 9, out, sizeof(out));
    CHECK(out[0] == '\0');  // Unknown encoding.
}

void testTags(void)
{
    testId3v23();
    testId3v24();
    testId3v22AndUnsync();
    testId3Malformed();
    testId3v1();
    testVorbisComments();
    testFlacPictureMalformed();
    testText();
}
