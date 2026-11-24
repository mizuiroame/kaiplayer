// Text and byte helpers shared by the tag parsers.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tags.h"

// ID3 text encodings.
enum {
    TEXT_LATIN1 = 0,
    TEXT_UTF16_BOM = 1,
    TEXT_UTF16_BE = 2,
    TEXT_UTF8 = 3,
};

// Converts text in an ID3 encoding to UTF-8 in out, stopping at the first
// NUL. Invalid input becomes '?'. out is always terminated.
void textToUtf8(const uint8_t *src, size_t length, int encoding, char *out, size_t out_size);

// Copies text into a tag field only if the field is still empty.
void textSetField(char *field, const char *value);

// Takes a picture if it is better than the one kept: any first picture, or
// a front cover replacing another type. Copies the bytes.
void textOfferPicture(TrackTags *t, const uint8_t *data, size_t size, int type);

// Keeps the first lyrics offered (UTF-8, len bytes) when t wants them.
void textOfferLyrics(TrackTags *t, const char *text, size_t len);

uint32_t readBe32(const uint8_t *p);
uint32_t readBe24(const uint8_t *p);
uint32_t readLe32(const uint8_t *p);
// ID3 "syncsafe" integer: 4 bytes of 7 bits each.
uint32_t readSyncsafe32(const uint8_t *p);
