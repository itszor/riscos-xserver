#include <string.h>
#include "libsha1.h"

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void block(sha1_ctx *c, const unsigned char *p)
{
    uint32_t w[80], a, b, d, e, f, k, t, cc;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 |
               (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (; i < 80; i++)
        w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & cc) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;                    k = 0xCA62C1D6; }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = ROL(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

void sha1_begin(sha1_ctx *c)
{
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0; c->used = 0;
}

void sha1_hash(const unsigned char *data, unsigned long len, sha1_ctx *c)
{
    c->len += len;
    while (len) {
        unsigned int n = 64 - c->used;
        if (n > len) n = len;
        memcpy(c->buf + c->used, data, n);
        c->used += n; data += n; len -= n;
        if (c->used == 64) { block(c, c->buf); c->used = 0; }
    }
}

void sha1_end(unsigned char out[SHA1_DIGEST_SIZE], sha1_ctx *c)
{
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80, zero = 0, lenb[8];
    int i;
    sha1_hash(&pad, 1, c);
    while (c->used != 56) sha1_hash(&zero, 1, c);
    for (i = 0; i < 8; i++) lenb[i] = (unsigned char)(bits >> (56 - 8*i));
    sha1_hash(lenb, 8, c);
    for (i = 0; i < 20; i++) out[i] = (unsigned char)(c->h[i/4] >> (24 - 8*(i%4)));
}
