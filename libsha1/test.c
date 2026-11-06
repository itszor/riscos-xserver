#include <stdio.h>
#include <string.h>
#include "libsha1.h"
static int check(const char *msg, unsigned long reps, const char *want) {
    sha1_ctx c; unsigned char d[20]; char hex[41]; unsigned long i; int j;
    sha1_begin(&c);
    for (i = 0; i < reps; i++) sha1_hash((const unsigned char *)msg, strlen(msg), &c);
    sha1_end(d, &c);
    for (j = 0; j < 20; j++) sprintf(hex + 2*j, "%02x", d[j]);
    printf("%s %s\n", strcmp(hex, want) ? "FAIL" : "ok  ", hex);
    return strcmp(hex, want) != 0;
}
int main(void) {
    int f = 0;
    f |= check("abc", 1, "a9993e364706816aba3e25717850c26c9cd0d89d");
    f |= check("", 1, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    f |= check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
               "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    f |= check("a", 1000000, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    return f;
}
