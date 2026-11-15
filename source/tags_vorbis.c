// Vorbis comments ("KEY=value", used by FLAC and Ogg) and FLAC PICTURE blocks.
#include <stdlib.h>
#include <string.h>

#include "tags.h"
#include "tags_text.h"
#include "util.h"

#define MAX_COMMENTS 4096
#define MAX_KEY 32
#define PICTURE_BLOCK_OVERHEAD (64u << 10)

void tagsParseFlacPicture(const uint8_t *data, size_t size, TrackTags *t)
{
    // type, MIME length + MIME, description length + description,
    // width, height, depth, colors, data length + data. All big-endian.
    size_t pos = 0;
    if (size < 8)
        return;
    int type = (int)readBe32(data);
    pos = 4;
    for (int skip = 0; skip < 2; skip++) {
        if (size - pos < 4)
            return;
        uint32_t len = readBe32(data + pos);
        pos += 4;
        if (len > size - pos)
            return;
        pos += len;
    }
    if (size - pos < 20)
        return;
    pos += 16;
    uint32_t data_len = readBe32(data + pos);
    pos += 4;
    if (data_len > size - pos)
        return;
    textOfferPicture(t, data + pos, data_len, type);
}

static int base64Value(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Decodes base64 until padding or an invalid character. Caller frees.
static uint8_t *base64Decode(const char *src, size_t len, size_t *out_len)
{
    uint8_t *out = malloc(len / 4 * 3 + 3);
    if (!out)
        return NULL;
    size_t n = 0;
    uint32_t bits = 0;
    int count = 0;
    for (size_t i = 0; i < len; i++) {
        int v = base64Value((uint8_t)src[i]);
        if (v < 0)
            break;
        bits = bits << 6 | (uint32_t)v;
        if (++count == 4) {
            out[n++] = (uint8_t)(bits >> 16);
            out[n++] = (uint8_t)(bits >> 8);
            out[n++] = (uint8_t)bits;
            bits = 0;
            count = 0;
        }
    }
    if (count == 3) {
        out[n++] = (uint8_t)(bits >> 10);
        out[n++] = (uint8_t)(bits >> 2);
    } else if (count == 2) {
        out[n++] = (uint8_t)(bits >> 4);
    }
    *out_len = n;
    return out;
}

static bool keyIs(const char *key, size_t key_len, const char *name)
{
    char buf[MAX_KEY + 1];
    if (key_len > MAX_KEY)
        return false;
    memcpy(buf, key, key_len);
    buf[key_len] = '\0';
    return utilCompareNames(buf, name) == 0;
}

static void setTextField(char *field, const char *value, size_t len)
{
    char text[TAGS_TEXT_MAX];
    if (len >= sizeof(text))
        len = sizeof(text) - 1;
    memcpy(text, value, len);
    text[len] = '\0';
    textSetField(field, text);
}

void tagsApplyComment(const char *comment, size_t length, TrackTags *t)
{
    const char *eq = memchr(comment, '=', length);
    if (!eq)
        return;
    size_t key_len = (size_t)(eq - comment);
    const char *value = eq + 1;
    size_t value_len = length - key_len - 1;

    if (keyIs(comment, key_len, "TITLE"))
        setTextField(t->title, value, value_len);
    else if (keyIs(comment, key_len, "ARTIST"))
        setTextField(t->artist, value, value_len);
    else if (keyIs(comment, key_len, "ALBUM"))
        setTextField(t->album, value, value_len);
    else if (keyIs(comment, key_len, "ALBUMARTIST"))
        setTextField(t->album_artist, value, value_len);
    else if (keyIs(comment, key_len, "METADATA_BLOCK_PICTURE") &&
             value_len / 4 * 3 <= TAGS_PICTURE_MAX + PICTURE_BLOCK_OVERHEAD) {
        size_t decoded_len = 0;
        uint8_t *decoded = base64Decode(value, value_len, &decoded_len);
        if (decoded)
            tagsParseFlacPicture(decoded, decoded_len, t);
        free(decoded);
    }
}

void tagsParseVorbisComments(const uint8_t *data, size_t size, TrackTags *t)
{
    // Lengths in a Vorbis comment block are little-endian.
    if (size < 4)
        return;
    uint32_t vendor_len = readLe32(data);
    size_t pos = 4;
    if (vendor_len > size - pos)
        return;
    pos += vendor_len;
    if (size - pos < 4)
        return;
    uint32_t count = readLe32(data + pos);
    pos += 4;
    for (uint32_t i = 0; i < count && i < MAX_COMMENTS; i++) {
        if (size - pos < 4)
            return;
        uint32_t len = readLe32(data + pos);
        pos += 4;
        if (len > size - pos)
            return;
        tagsApplyComment((const char *)data + pos, len, t);
        pos += len;
    }
}
