#include "md5.h"

#include <stdio.h>
#include <string.h>

// RFC 1321, one 64-byte block at a time.
typedef struct {
    uint32_t state[4];
    uint64_t length;  // Bytes so far.
    uint8_t block[64];
    size_t used;
} Md5;

static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static const uint8_t SHIFT[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static uint32_t rotate(uint32_t x, unsigned n)
{
    return x << n | x >> (32 - n);
}

static void transform(Md5 *m, const uint8_t *p)
{
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 |
               (uint32_t)p[i * 4 + 3] << 24;
    uint32_t a = m->state[0], b = m->state[1], c = m->state[2], d = m->state[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        uint32_t next = d;
        d = c;
        c = b;
        b = b + rotate(a + f + K[i] + w[g], SHIFT[i]);
        a = next;
    }
    m->state[0] += a;
    m->state[1] += b;
    m->state[2] += c;
    m->state[3] += d;
}

static void update(Md5 *m, const uint8_t *data, size_t size)
{
    m->length += size;
    while (size) {
        size_t n = sizeof(m->block) - m->used < size ? sizeof(m->block) - m->used : size;
        memcpy(m->block + m->used, data, n);
        m->used += n;
        data += n;
        size -= n;
        if (m->used == sizeof(m->block)) {
            transform(m, m->block);
            m->used = 0;
        }
    }
}

void md5Hex(const void *data, size_t size, char out[MD5_HEX_SIZE])
{
    Md5 m = {.state = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476}};
    update(&m, data, size);
    uint64_t bits = m.length * 8;
    static const uint8_t PAD[64] = {0x80};
    update(&m, PAD, m.used < 56 ? 56 - m.used : 120 - m.used);
    uint8_t tail[8];
    for (int i = 0; i < 8; i++)
        tail[i] = (uint8_t)(bits >> (8 * i));
    update(&m, tail, sizeof(tail));
    for (int i = 0; i < 16; i++)
        snprintf(out + i * 2, 3, "%02x", (unsigned)(m.state[i / 4] >> (8 * (i % 4)) & 0xFF));
    memset(&m, 0, sizeof(m));  // It held the password.
}
