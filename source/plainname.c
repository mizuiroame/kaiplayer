#include "plainname.h"

#include <stdio.h>
#include <string.h>

#include "util.h"

// Characters kept as they are, besides letters and digits.
#define PUNCTUATION " -_.,()'&!+[]"

// U+00C0..U+017F (Latin-1 letters and Latin Extended-A) without their
// accents; "_" for the ones with no plain look-alike.
static const char LATIN[] =
    "AAAAAAACEEEEIIIIDNOOOOOxOUUUUY_saaaaaaaceeeeiiiidnooooo_ouuuuy_y"
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGg"
    "GgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlL"
    "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSs"
    "SsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

_Static_assert(sizeof(LATIN) - 1 == 0x180 - 0xC0, "one letter per code point");

static bool kept(unsigned c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           (c && c < 0x80 && strchr(PUNCTUATION, (int)c));
}

// The plain look-alike of a code point, '\0' to drop it, '_' for none.
static char plainChar(unsigned cp)
{
    if (kept(cp))
        return (char)cp;
    if (cp >= 0xC0 && cp < 0x180)
        return LATIN[cp - 0xC0];
    switch (cp) {
    case 0xA0: return ' ';                     // No-break space.
    case 0xA1: case 0xBF: return '\0';         // ¡ ¿
    case 0x2018: case 0x2019: case 0x201C: case 0x201D: case 0xAB: case 0xBB: return '\'';
    case 0x2010: case 0x2011: case 0x2013: case 0x2014: return '-';
    default: return '_';
    }
}

static bool readable(const char *s)
{
    for (; *s; s++)
        if (kept((unsigned char)*s) && !strchr(" _.,-'", *s))
            return true;
    return false;
}

void plainName(const char *utf8, const char *fallback, char *out, size_t size)
{
    size_t n = 0;
    while (*utf8 && n + 1 < size) {
        char c = plainChar(utilNextCodePoint(&utf8));
        if (!c || (c == '_' && n > 0 && out[n - 1] == '_'))
            continue;
        if (c == ' ' && (n == 0 || out[n - 1] == ' '))
            continue;  // No leading or doubled spaces.
        out[n++] = c;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.'))
        n--;
    out[n] = '\0';
    size_t lead = strspn(out, ".");  // A leading dot hides a file.
    memmove(out, out + lead, n - lead + 1);
    if (!readable(out))
        snprintf(out, size, "%s", fallback);
}

bool plainNameIs(const char *name)
{
    for (; *name; name++)
        if (!kept((unsigned char)*name))
            return false;
    return true;
}
