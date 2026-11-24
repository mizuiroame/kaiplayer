// A small JSON reader for servers' answers (subsonic.h): the whole text into
// a tree, read with jsonGet and its helpers. Takes untrusted input: every
// length and depth is checked, and anything that isn't JSON fails.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_FALSE,
    JSON_TRUE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

typedef struct JsonValue JsonValue;
struct JsonValue {
    JsonType type;
    const char *key;   // Its name, in an object; NULL in an array.
    const char *text;  // A string's text (UTF-8, NUL-terminated); "" for the others.
    double number;
    JsonValue *child;  // An array's or object's first value.
    JsonValue *next;   // The next one in the same array or object.
};

typedef struct JsonDoc JsonDoc;

#define JSON_DEPTH_MAX 64
// Values in one answer, at most (a page of 500 songs has about 20,000): a
// server's 16 MB answer of tiny values can't take hundreds of MB.
#define JSON_VALUES_MAX 200000

// NULL if text isn't one JSON value (or memory runs out).
JsonDoc *jsonParse(const char *text, size_t size);
void jsonFree(JsonDoc *doc);
const JsonValue *jsonRoot(const JsonDoc *doc);

// An object's member, or NULL (also when v isn't an object).
const JsonValue *jsonGet(const JsonValue *v, const char *key);
// A member's string, or "" (not there, or not a string).
const char *jsonText(const JsonValue *v, const char *key);
// A member's number (a number, or a string of digits), or 0.
long long jsonInt(const JsonValue *v, const char *key);
double jsonNumber(const JsonValue *v, const char *key);
// True if the member is there as a number.
bool jsonHasNumber(const JsonValue *v, const char *key);
