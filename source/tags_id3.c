// ID3v2.2, v2.3 and v2.4 (start of MP3 files) and ID3v1 (last 128 bytes).
#include <stdlib.h>
#include <string.h>

#include "tags.h"
#include "tags_text.h"

#define ID3_HEADER_SIZE 10
#define ID3_FLAG_UNSYNC 0x80
#define ID3_FLAG_EXTENDED 0x40
#define ID3V1_SIZE 128
#define ID3V1_FIELD 30

typedef struct {
    const char *title, *artist, *album, *album_artist, *picture;
} FrameIds;

static const FrameIds IDS_V22 = {"TT2", "TP1", "TAL", "TP2", "PIC"};
static const FrameIds IDS_V23 = {"TIT2", "TPE1", "TALB", "TPE2", "APIC"};

// Undoes ID3 unsynchronization in place: drops each 0x00 that follows 0xFF.
static size_t resynchronize(uint8_t *data, size_t size)
{
    size_t w = 0;
    for (size_t r = 0; r < size; r++) {
        data[w++] = data[r];
        if (data[r] == 0xFF && r + 1 < size && data[r + 1] == 0x00)
            r++;
    }
    return w;
}

// Length of a string in the given encoding, with its terminator if present.
static size_t terminatedLength(const uint8_t *p, size_t size, int encoding)
{
    if (encoding == TEXT_UTF16_BOM || encoding == TEXT_UTF16_BE) {
        for (size_t i = 0; i + 1 < size; i += 2) {
            if (p[i] == 0 && p[i + 1] == 0)
                return i + 2;
        }
        return size;
    }
    for (size_t i = 0; i < size; i++) {
        if (p[i] == 0)
            return i + 1;
    }
    return size;
}

static void parseTextFrame(const uint8_t *body, size_t size, char *field)
{
    if (size < 2)
        return;
    char text[TAGS_TEXT_MAX];
    textToUtf8(body + 1, size - 1, body[0], text, sizeof(text));
    textSetField(field, text);
}

// APIC: encoding, MIME type, picture type, description, data.
// PIC (v2.2): encoding, 3-byte format, picture type, description, data.
static void parsePictureFrame(const uint8_t *body, size_t size, bool v22, TrackTags *t)
{
    if (size < 1)
        return;
    int encoding = body[0];
    size_t pos = 1;
    if (v22)
        pos += 3;
    else
        pos += terminatedLength(body + pos, size - pos, TEXT_LATIN1);
    if (pos >= size)
        return;
    int type = body[pos++];
    pos += terminatedLength(body + pos, size - pos, encoding);
    if (pos < size)
        textOfferPicture(t, body + pos, size - pos, type);
}

static void parseFrame(const uint8_t *id, const uint8_t *body, size_t size, bool v22, TrackTags *t)
{
    const FrameIds *ids = v22 ? &IDS_V22 : &IDS_V23;
    size_t n = v22 ? 3 : 4;
    if (memcmp(id, ids->title, n) == 0)
        parseTextFrame(body, size, t->title);
    else if (memcmp(id, ids->artist, n) == 0)
        parseTextFrame(body, size, t->artist);
    else if (memcmp(id, ids->album, n) == 0)
        parseTextFrame(body, size, t->album);
    else if (memcmp(id, ids->album_artist, n) == 0)
        parseTextFrame(body, size, t->album_artist);
    else if (memcmp(id, ids->picture, n) == 0)
        parsePictureFrame(body, size, v22, t);
}

// Strips the per-frame extras of v2.3/v2.4. Returns false for frames we skip
// (compressed or encrypted).
static bool unwrapFrame(int major, uint8_t flags, bool tag_unsync, uint8_t **body, size_t *size)
{
    if (major == 3) {
        if (flags & 0xC0)
            return false;
        if (flags & 0x20) {  // Grouping identity.
            if (*size < 1)
                return false;
            (*body)++, (*size)--;
        }
        return true;
    }
    if (flags & 0x0C)
        return false;
    if (flags & 0x40) {  // Grouping identity.
        if (*size < 1)
            return false;
        (*body)++, (*size)--;
    }
    if (flags & 0x01) {  // Data length indicator.
        if (*size < 4)
            return false;
        *body += 4, *size -= 4;
    }
    if ((flags & 0x02) || tag_unsync)
        *size = resynchronize(*body, *size);
    return true;
}

static void parseFrames(uint8_t *buf, size_t len, int major, bool tag_unsync, TrackTags *t)
{
    bool v22 = major == 2;
    size_t header = v22 ? 6 : 10;
    size_t pos = 0;
    while (pos + header <= len) {
        const uint8_t *h = buf + pos;
        if (h[0] == 0)
            break;  // Padding.
        size_t frame_size = v22 ? readBe24(h + 3)
                          : major == 4 ? readSyncsafe32(h + 4) : readBe32(h + 4);
        uint8_t flags = v22 ? 0 : h[9];
        pos += header;
        if (frame_size > len - pos)
            break;
        uint8_t *body = buf + pos;
        size_t body_size = frame_size;
        if (v22 || unwrapFrame(major, flags, tag_unsync, &body, &body_size))
            parseFrame(h, body, body_size, v22, t);
        pos += frame_size;
    }
}

void tagsParseId3v2(const uint8_t *data, size_t size, TrackTags *t)
{
    if (size < ID3_HEADER_SIZE || memcmp(data, "ID3", 3) != 0)
        return;
    int major = data[3];
    uint8_t flags = data[5];
    if (major < 2 || major > 4)
        return;
    size_t len = readSyncsafe32(data + 6);
    if (len > size - ID3_HEADER_SIZE)
        len = size - ID3_HEADER_SIZE;
    if (len == 0)
        return;

    // Work on a copy: resynchronization rewrites bytes.
    uint8_t *buf = malloc(len);
    if (!buf)
        return;
    memcpy(buf, data + ID3_HEADER_SIZE, len);
    bool unsync = (flags & ID3_FLAG_UNSYNC) != 0;
    if (unsync && major < 4)
        len = resynchronize(buf, len);

    size_t start = 0;
    if ((flags & ID3_FLAG_EXTENDED) && major >= 3 && len >= 4) {
        size_t ext = major == 4 ? readSyncsafe32(buf) : (size_t)readBe32(buf) + 4;
        start = ext <= len ? ext : len;
    }
    parseFrames(buf + start, len - start, major, unsync && major == 4, t);
    free(buf);
}

static void setLatin1Field(char *field, const uint8_t *src)
{
    size_t n = ID3V1_FIELD;
    while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == 0))
        n--;
    char text[TAGS_TEXT_MAX];
    textToUtf8(src, n, TEXT_LATIN1, text, sizeof(text));
    textSetField(field, text);
}

void tagsParseId3v1(const uint8_t *data, size_t size, TrackTags *t)
{
    if (size < ID3V1_SIZE || memcmp(data, "TAG", 3) != 0)
        return;
    setLatin1Field(t->title, data + 3);
    setLatin1Field(t->artist, data + 33);
    setLatin1Field(t->album, data + 63);
}
