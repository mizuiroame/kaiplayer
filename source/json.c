#include "json.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_VALUES 1024

typedef struct Chunk {
    struct Chunk *next;
    size_t used;
    JsonValue values[CHUNK_VALUES];
} Chunk;

struct JsonDoc {
    char *text;  // A copy of the input; strings are decoded in it, in place.
    Chunk *chunks;
    JsonValue *root;
};

typedef struct {
    JsonDoc *doc;
    char *at, *end;
    bool failed;
    size_t values;  // Made so far (JSON_VALUES_MAX at most).
} Parser;

static JsonValue *newValue(Parser *p, JsonType type)
{
    if (++p->values > JSON_VALUES_MAX) {
        p->failed = true;
        return NULL;
    }
    Chunk *c = p->doc->chunks;
    if (!c || c->used == CHUNK_VALUES) {
        c = malloc(sizeof(Chunk));
        if (!c) {
            p->failed = true;
            return NULL;
        }
        c->next = p->doc->chunks;
        c->used = 0;
        p->doc->chunks = c;
    }
    JsonValue *v = &c->values[c->used++];
    *v = (JsonValue){.type = type, .text = ""};
    return v;
}

static void skipSpace(Parser *p)
{
    while (p->at < p->end && (*p->at == ' ' || *p->at == '\t' || *p->at == '\n' || *p->at == '\r'))
        p->at++;
}

static int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Four hex digits at s, or -1.
static long hex4(const char *s, const char *end)
{
    if (end - s < 4)
        return -1;
    long v = 0;
    for (int i = 0; i < 4; i++) {
        int d = hexDigit(s[i]);
        if (d < 0)
            return -1;
        v = v * 16 + d;
    }
    return v;
}

static char *putUtf8(char *out, uint32_t c)
{
    if (c < 0x80) {
        *out++ = (char)c;
    } else if (c < 0x800) {
        *out++ = (char)(0xC0 | c >> 6);
        *out++ = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        *out++ = (char)(0xE0 | c >> 12);
        *out++ = (char)(0x80 | (c >> 6 & 0x3F));
        *out++ = (char)(0x80 | (c & 0x3F));
    } else {
        *out++ = (char)(0xF0 | c >> 18);
        *out++ = (char)(0x80 | (c >> 12 & 0x3F));
        *out++ = (char)(0x80 | (c >> 6 & 0x3F));
        *out++ = (char)(0x80 | (c & 0x3F));
    }
    return out;
}

// "\uXXXX" (with its pair, for one outside the BMP) at p->at, after the 'u'. Writes it at out.
static char *escapedCodePoint(Parser *p, char *out)
{
    long c = hex4(p->at, p->end);
    if (c < 0) {
        p->failed = true;
        return out;
    }
    p->at += 4;
    if (c >= 0xD800 && c <= 0xDBFF && p->end - p->at >= 6 && p->at[0] == '\\' && p->at[1] == 'u') {
        long low = hex4(p->at + 2, p->end);
        if (low >= 0xDC00 && low <= 0xDFFF) {
            p->at += 6;
            return putUtf8(out, 0x10000 + (((uint32_t)c - 0xD800) << 10) + ((uint32_t)low - 0xDC00));
        }
    }
    if (c >= 0xD800 && c <= 0xDFFF)
        c = 0xFFFD;  // Half a pair.
    return putUtf8(out, (uint32_t)c);
}

// A string at p->at (its '"'), decoded where it is. Its text, or NULL.
static const char *parseString(Parser *p)
{
    p->at++;
    char *start = p->at, *out = p->at;
    while (!p->failed) {
        if (p->at >= p->end) {
            p->failed = true;
            break;
        }
        char c = *p->at++;
        if (c == '"') {
            *out = '\0';  // Never past the closing quote: decoding only shortens.
            return start;
        }
        if ((unsigned char)c < 0x20) {
            p->failed = true;
            break;
        }
        if (c != '\\') {
            *out++ = c;
            continue;
        }
        if (p->at >= p->end) {
            p->failed = true;
            break;
        }
        char e = *p->at++;
        switch (e) {
        case '"': *out++ = '"'; break;
        case '\\': *out++ = '\\'; break;
        case '/': *out++ = '/'; break;
        case 'b': *out++ = '\b'; break;
        case 'f': *out++ = '\f'; break;
        case 'n': *out++ = '\n'; break;
        case 'r': *out++ = '\r'; break;
        case 't': *out++ = '\t'; break;
        case 'u': out = escapedCodePoint(p, out); break;
        default: p->failed = true; break;
        }
    }
    return NULL;
}

static bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

static size_t digits(const char *s, const char *end)
{
    size_t n = 0;
    while (s + n < end && isDigit(s[n]))
        n++;
    return n;
}

// JSON's number grammar, then strtod on a copy of it.
static void parseNumber(Parser *p, JsonValue *v)
{
    char *s = p->at;
    size_t n = s < p->end && *s == '-' ? 1 : 0;
    size_t whole = digits(s + n, p->end);
    if (!whole || (whole > 1 && s[n] == '0')) {
        p->failed = true;
        return;
    }
    n += whole;
    if (s + n < p->end && s[n] == '.') {
        size_t fraction = digits(s + n + 1, p->end);
        if (!fraction) {
            p->failed = true;
            return;
        }
        n += 1 + fraction;
    }
    if (s + n < p->end && (s[n] == 'e' || s[n] == 'E')) {
        n++;
        if (s + n < p->end && (s[n] == '+' || s[n] == '-'))
            n++;
        size_t exponent = digits(s + n, p->end);
        if (!exponent) {
            p->failed = true;
            return;
        }
        n += exponent;
    }
    char copy[64];
    if (n >= sizeof(copy)) {
        p->failed = true;
        return;
    }
    memcpy(copy, s, n);
    copy[n] = '\0';
    v->number = strtod(copy, NULL);
    p->at += n;
}

static bool literal(Parser *p, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(p->end - p->at) < n || memcmp(p->at, word, n) != 0)
        return false;
    p->at += n;
    return true;
}

static JsonValue *parseValue(Parser *p, int depth);

// The members of an array or object, after its opening bracket.
static void parseItems(Parser *p, JsonValue *v, int depth)
{
    char close = v->type == JSON_ARRAY ? ']' : '}';
    JsonValue **tail = &v->child;
    skipSpace(p);
    if (p->at < p->end && *p->at == close) {
        p->at++;
        return;
    }
    while (!p->failed) {
        const char *key = NULL;
        if (v->type == JSON_OBJECT) {
            skipSpace(p);
            if (p->at >= p->end || *p->at != '"' || !(key = parseString(p))) {
                p->failed = true;
                return;
            }
            skipSpace(p);
            if (p->at >= p->end || *p->at != ':') {
                p->failed = true;
                return;
            }
            p->at++;
        }
        JsonValue *item = parseValue(p, depth + 1);
        if (!item)
            return;
        item->key = key;
        *tail = item;
        tail = &item->next;
        skipSpace(p);
        if (p->at < p->end && *p->at == ',') {
            p->at++;
            continue;
        }
        if (p->at < p->end && *p->at == close) {
            p->at++;
            return;
        }
        p->failed = true;
    }
}

static JsonValue *parseValue(Parser *p, int depth)
{
    skipSpace(p);
    if (depth > JSON_DEPTH_MAX || p->at >= p->end) {
        p->failed = true;
        return NULL;
    }
    char c = *p->at;
    JsonValue *v = NULL;
    if (c == '{' || c == '[') {
        p->at++;
        v = newValue(p, c == '{' ? JSON_OBJECT : JSON_ARRAY);
        if (v)
            parseItems(p, v, depth);
    } else if (c == '"') {
        v = newValue(p, JSON_STRING);
        const char *text = v ? parseString(p) : NULL;
        if (v && text)
            v->text = text;
    } else if (c == '-' || isDigit(c)) {
        v = newValue(p, JSON_NUMBER);
        if (v)
            parseNumber(p, v);
    } else if (literal(p, "true")) {
        v = newValue(p, JSON_TRUE);
    } else if (literal(p, "false")) {
        v = newValue(p, JSON_FALSE);
    } else if (literal(p, "null")) {
        v = newValue(p, JSON_NULL);
    } else {
        p->failed = true;
    }
    return p->failed ? NULL : v;
}

void jsonFree(JsonDoc *doc)
{
    if (!doc)
        return;
    while (doc->chunks) {
        Chunk *next = doc->chunks->next;
        free(doc->chunks);
        doc->chunks = next;
    }
    free(doc->text);
    free(doc);
}

JsonDoc *jsonParse(const char *text, size_t size)
{
    JsonDoc *doc = text ? calloc(1, sizeof(*doc)) : NULL;
    if (!doc)
        return NULL;
    doc->text = malloc(size + 1);
    if (!doc->text) {
        free(doc);
        return NULL;
    }
    memcpy(doc->text, text, size);
    doc->text[size] = '\0';
    Parser p = {doc, doc->text, doc->text + size, false, 0};
    doc->root = parseValue(&p, 0);
    skipSpace(&p);
    if (!doc->root || p.failed || p.at != p.end) {
        jsonFree(doc);
        return NULL;
    }
    return doc;
}

const JsonValue *jsonRoot(const JsonDoc *doc)
{
    return doc ? doc->root : NULL;
}

const JsonValue *jsonGet(const JsonValue *v, const char *key)
{
    if (!v || v->type != JSON_OBJECT)
        return NULL;
    for (const JsonValue *m = v->child; m; m = m->next)
        if (!strcmp(m->key, key))
            return m;
    return NULL;
}

const char *jsonText(const JsonValue *v, const char *key)
{
    const JsonValue *m = jsonGet(v, key);
    return m && m->type == JSON_STRING ? m->text : "";
}

double jsonNumber(const JsonValue *v, const char *key)
{
    const JsonValue *m = jsonGet(v, key);
    if (m && m->type == JSON_NUMBER)
        return m->number;
    if (m && m->type == JSON_STRING && m->text[0])
        return strtod(m->text, NULL);
    return 0;
}

long long jsonInt(const JsonValue *v, const char *key)
{
    double d = jsonNumber(v, key);
    return d > 9e18 || d < -9e18 ? 0 : (long long)d;
}

bool jsonHasNumber(const JsonValue *v, const char *key)
{
    const JsonValue *m = jsonGet(v, key);
    return m && m->type == JSON_NUMBER;
}
