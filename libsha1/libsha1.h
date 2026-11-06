/* Minimal SHA-1 with the libsha1 API that xorg-server's os/xsha1.c expects.
 * Written for the RISC OS X server port; SHA-1 per FIPS 180-4. */
#ifndef LIBSHA1_H
#define LIBSHA1_H
#include <stdint.h>
#include <stddef.h>

#define SHA1_DIGEST_SIZE 20

typedef struct {
    uint32_t h[5];
    uint64_t len;          /* bytes hashed so far */
    unsigned char buf[64];
    unsigned int used;     /* bytes waiting in buf */
} sha1_ctx;

void sha1_begin(sha1_ctx *ctx);
void sha1_hash(const unsigned char *data, unsigned long len, sha1_ctx *ctx);
void sha1_end(unsigned char hval[SHA1_DIGEST_SIZE], sha1_ctx *ctx);
#endif
