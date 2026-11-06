/* xping: connect to an X server over TCP and perform the connection setup
 * handshake. Prints the vendor and release on success. Runs on RISC OS.
 * Usage: xping [host] [display] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

static double now(void) { struct timeval t; gettimeofday(&t, NULL); return t.tv_sec + t.tv_usec / 1e6; }

static int readn(int s, unsigned char *b, int n) {
  int got = 0;
  while (got < n) { int r = read(s, b + got, n - got); if (r <= 0) return -1; got += r; }
  return got;
}

int main(int argc, char **argv) {
  const char *host = argc > 1 ? argv[1] : "127.0.0.1";
  int dpy = argc > 2 ? atoi(argv[2]) : 0;
  double t0 = now();
  int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(6000 + dpy); a.sin_addr.s_addr = inet_addr(host);
  /* Connect with a 5 s timeout, so a dead network cannot hang the caller. */
  fcntl(s, F_SETFL, O_NONBLOCK);
  if (connect(s, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) {
    perror("xping: connect"); return 1;
  }
  fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
  struct timeval ct = {5, 0};
  if (select(s + 1, NULL, &wf, NULL, &ct) != 1) { printf("xping: connect timed out\n"); return 1; }
  int soerr = 0; socklen_t sl = sizeof soerr;
  getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &sl);
  if (soerr) { errno = soerr; perror("xping: connect"); return 1; }
  fcntl(s, F_SETFL, 0);
  /* Little-endian setup request, protocol 11.0, no authorisation. */
  unsigned char req[12] = { 'l', 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  if (write(s, req, sizeof req) != sizeof req) { perror("xping: write"); return 1; }
  unsigned char hdr[8];
  if (readn(s, hdr, 8) < 0) { perror("xping: read"); return 1; }
  int extra = (hdr[6] | hdr[7] << 8) * 4;
  unsigned char *body = malloc(extra);
  if (readn(s, body, extra) < 0) { perror("xping: read body"); return 1; }
  if (hdr[0] != 1) { printf("xping: refused: %.*s\n", hdr[1], body); return 1; }
  unsigned rel = body[0] | body[1] << 8 | body[2] << 16 | (unsigned)body[3] << 24;
  int vlen = body[16] | body[17] << 8;
  printf("xping: %s:%d OK protocol %d.%d, vendor \"%.*s\" release %u, %.3fs\n",
         host, dpy, hdr[2] | hdr[3] << 8, hdr[4] | hdr[5] << 8, vlen, body + 32, rel, now() - t0);
  if (argc > 3 && !strcmp(argv[3], "getimage")) {
    /* Find the first screen: after the 32-byte fixed part, the padded vendor
       string and the pixmap formats. */
    int nformats = body[21];
    unsigned char *scr = body + 32 + ((vlen + 3) & ~3) + 8 * nformats;
    unsigned root = scr[0] | scr[1] << 8 | scr[2] << 16 | (unsigned)scr[3] << 24;
    int w = scr[20] | scr[21] << 8, h = scr[22] | scr[23] << 8;
    unsigned char gi[20] = { 73, 2, 5, 0 };            /* GetImage, ZPixmap, 5 words */
    memcpy(gi + 4, &root, 4);                           /* little-endian host */
    gi[12] = w & 255; gi[13] = w >> 8; gi[14] = h & 255; gi[15] = h >> 8;
    memset(gi + 16, 0xff, 4);                           /* plane mask */
    double t1 = now();
    if (write(s, gi, sizeof gi) != sizeof gi) { perror("xping: write GetImage"); return 1; }
    unsigned char rh[32];
    if (readn(s, rh, 32) < 0) { perror("xping: read GetImage reply"); return 1; }
    if (rh[0] != 1) { printf("xping: GetImage error code %d\n", rh[1]); return 1; }
    unsigned len = (rh[4] | rh[5] << 8 | rh[6] << 16 | (unsigned)rh[7] << 24) * 4;
    unsigned char *img = malloc(len);
    if (!img || readn(s, img, len) < 0) { perror("xping: read image"); return 1; }
    double dt = now() - t1;
    printf("xping: GetImage %dx%d depth %d: %u bytes in %.3fs (%.0f KB/s)\n",
           w, h, rh[1], len, dt, len / 1024.0 / (dt > 0 ? dt : 1e-6));
    if (argc > 4) {                                     /* raw dump for checking */
      FILE *f = fopen(argv[4], "wb");
      if (f) { fwrite(img, 1, len, f); fclose(f); printf("xping: wrote %s\n", argv[4]); }
    }
  }
  close(s);
  return 0;
}
