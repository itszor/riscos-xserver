/* Phase 0 smoke test: does GCCSDK 4.7.4 + UnixLib work on RISC OS 3.70?
 * Exercises the facilities an X server needs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int fails;
#define CHECK(name, cond) do { int ok_ = (cond); \
  printf("%-28s %s\n", name, ok_ ? "PASS" : "FAIL"); \
  if (!ok_) { fails++; printf("    errno=%d %s\n", errno, strerror(errno)); } } while (0)

static double now(void) {
  struct timeval tv; gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

static volatile sig_atomic_t alarms;
static void on_alarm(int s) { (void)s; alarms++; }

int main(void) {
  struct utsname u;
  CHECK("uname", uname(&u) == 0);
  printf("    %s %s %s %s\n", u.sysname, u.release, u.version, u.machine);

  /* Large heap: an X server wants several MB */
  size_t big = 12u << 20;
  char *p = malloc(big);
  CHECK("malloc 12MB", p != NULL);
  if (p) { memset(p, 0x5a, big); CHECK("touch 12MB", p[big - 1] == 0x5a); free(p); }

  /* Timers: the server's timer list uses these */
  double t0 = now();
  struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_alarm;
  sigaction(SIGALRM, &sa, NULL);
  struct itimerval it = { {0, 100000}, {0, 100000} };
  CHECK("setitimer", setitimer(ITIMER_REAL, &it, NULL) == 0);
  while (alarms < 5 && now() - t0 < 3) ;
  memset(&it, 0, sizeof it); setitimer(ITIMER_REAL, &it, NULL);
  printf("    5 alarms took %.2fs (expect ~0.5)\n", now() - t0);
  CHECK("SIGALRM delivered", alarms >= 5);

  /* select() with a timeout and no fds */
  struct timeval tv = {0, 500000};
  t0 = now();
  int r = select(0, NULL, NULL, NULL, &tv);
  printf("    select(0) slept %.2fs\n", now() - t0);
  CHECK("select timeout", r == 0);

  /* TCP loopback, as X clients would connect on :0 (port 6000) */
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  CHECK("socket", ls >= 0);
  int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(6099); a.sin_addr.s_addr = htonl(INADDR_ANY);
  CHECK("bind :6099", bind(ls, (struct sockaddr *)&a, sizeof a) == 0);
  CHECK("listen", listen(ls, 5) == 0);
  int cs = socket(AF_INET, SOCK_STREAM, 0);
  fcntl(cs, F_SETFL, O_NONBLOCK);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  r = connect(cs, (struct sockaddr *)&a, sizeof a);
  CHECK("connect 127.0.0.1", r == 0 || errno == EINPROGRESS);
  fd_set rf; FD_ZERO(&rf); FD_SET(ls, &rf);
  tv.tv_sec = 2; tv.tv_usec = 0;
  CHECK("select on listener", select(ls + 1, &rf, NULL, NULL, &tv) == 1);
  int ss = accept(ls, NULL, NULL);
  CHECK("accept", ss >= 0);
  fcntl(cs, F_SETFL, 0);
  const char msg[] = "X11 says hello";
  CHECK("send", write(cs, msg, sizeof msg) == (ssize_t)sizeof msg);
  char buf[64] = {0};
  FD_ZERO(&rf); FD_SET(ss, &rf); tv.tv_sec = 2;
  CHECK("select readable", select(ss + 1, &rf, NULL, NULL, &tv) == 1);
  CHECK("recv", read(ss, buf, sizeof buf) == (ssize_t)sizeof msg && !strcmp(buf, msg));
  close(cs); close(ss); close(ls);

  int sp[2];
  CHECK("socketpair", socketpair(AF_INET, SOCK_STREAM, 0, sp) == 0);

  struct hostent *h = gethostbyname("localhost");
  CHECK("gethostbyname localhost", h != NULL);

  /* Files with Unix paths */
  FILE *f = fopen("/tmp/x0smoke.txt", "w");
  CHECK("fopen /tmp", f != NULL);
  if (f) { fputs("ok\n", f); fclose(f); CHECK("unlink", unlink("/tmp/x0smoke.txt") == 0); }

  printf("RESULT: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}
