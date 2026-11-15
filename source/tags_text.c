#include "tags_text.h"

#include <stdlib.h>
#include <string.h>

#define FRONT_COVER 3

// Appends one code point as UTF-8 if it fits. Returns the new length.
static size_t putUtf8(char *out, size_t pos, size_t out_size, uint32_t cp)
{
    char buf[4];
    size_t n;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    if (pos + n >= out_size)
        return pos;
    memcpy(out + pos, buf, n);
    return pos + n;
}

static void utf16ToUtf8(const uint8_t *src, size_t length, bool big_endian, char *out, size_t out_size)
{
    size_t pos = 0;
    for (size_t i = 0; i + 1 < length; i += 2) {
        uint32_t unit = big_endian ? (uint32_t)(src[i] << 8 | src[i + 1])
                                   : (uint32_t)(src[i + 1] << 8 | src[i]);
        if (unit == 0)
            break;
        uint32_t cp = unit;
        if (unit >= 0xD800 && unit < 0xDC00 && i + 3 < length) {
            uint32_t low = big_endian ? (uint32_t)(src[i + 2] << 8 | src[i + 3])
                                      : (uint32_t)(src[i + 3] << 8 | src[i + 2]);
            if (low >= 0xDC00 && low < 0xE000) {
                cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            } else {
                cp = '?';
            }
        } else if (unit >= 0xD800 && unit < 0xE000) {
            cp = '?';
        }
        pos = putUtf8(out, pos, out_size, cp);
    }
    out[pos] = '\0';
}

void textToUtf8(const uint8_t *src, size_t length, int encoding, char *out, size_t out_size)
{
    if (out_size == 0)
        return;
    out[0] = '\0';
    size_t pos = 0;
    switch (encoding) {
    case TEXT_LATIN1:
        for (size_t i = 0; i < length && src[i]; i++)
            pos = putUtf8(out, pos, out_size, src[i]);
        out[pos] = '\0';
        break;
    case TEXT_UTF16_BOM: {
        bool big_endian = !(length >= 2 && src[0] == 0xFF && src[1] == 0xFE);
        bool has_bom = length >= 2 && ((src[0] == 0xFF && src[1] == 0xFE) ||
                                       (src[0] == 0xFE && src[1] == 0xFF));
        size_t skip = has_bom ? 2 : 0;
        utf16ToUtf8(src + skip, length - skip, big_endian, out, out_size);
        break;
    }
    case TEXT_UTF16_BE:
        utf16ToUtf8(src, length, true, out, out_size);
        break;
    case TEXT_UTF8: {
        size_t n = 0;
        while (n < length && src[n] && n + 1 < out_size)
            n++;
        if (n < length && src[n] && n > 0) {
            while (n > 0 && (src[n] & 0xC0) == 0x80)
                n--;  // Truncated: drop a cut multi-byte sequence.
        }
        memcpy(out, src, n);
        out[n] = '\0';
        break;
    }
    default:
        break;
    }
}

void textSetField(char *field, const char *value)
{
    if (field[0] == '\0' && value[0] != '\0') {
        size_t n = strlen(value);
        if (n >= TAGS_TEXT_MAX)
            n = TAGS_TEXT_MAX - 1;
        memcpy(field, value, n);
        field[n] = '\0';
    }
}

void textOfferPicture(TrackTags *t, const uint8_t *data, size_t size, int type)
{
    if (size == 0 || size > TAGS_PICTURE_MAX)
        return;
    bool better = !t->picture || (type == FRONT_COVER && t->picture_type != FRONT_COVER);
    if (!better)
        return;
    uint8_t *copy = malloc(size);
    if (!copy)
        return;
    memcpy(copy, data, size);
    free(t->picture);
    t->picture = copy;
    t->picture_size = size;
    t->picture_type = type;
}

uint32_t readBe32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint32_t readBe24(const uint8_t *p)
{
    return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

uint32_t readLe32(const uint8_t *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

uint32_t readSyncsafe32(const uint8_t *p)
{
    return (uint32_t)(p[0] & 0x7F) << 21 | (uint32_t)(p[1] & 0x7F) << 14 |
           (uint32_t)(p[2] & 0x7F) << 7 | (p[3] & 0x7F);
}
