/*
 * evtest: prototype of the X server's main loop as a RISC OS Wimp task.
 *
 * A TCP echo server on port 6000 driven by rwl_wait(), with a 1-second timer
 * standing in for the server's timers. Commands, each on a line of its own:
 *   stats   reply with the loop statistics
 *   quit    shut down
 * Anything else is echoed back. Run with *WimpTask; output goes to the file
 * named as the first argument, since a Wimp task has nowhere to print.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "rwl.h"

#define PORT		6000
#define MAX_CLIENTS	16
#define TICK_MS		1000

static FILE *logf;
static int listener = -1;
static int clients[MAX_CLIENTS];
static unsigned ticks, accepts, bytes_echoed, ready_wakeups, empty_wakeups;
static long max_tick_late_ms;

static void
logmsg(const char *fmt, ...)
{
	va_list ap;

	if (!logf)
		return;
	fprintf(logf, "[%6ld] ", rwl_now_ms());
	va_start(ap, fmt);
	vfprintf(logf, fmt, ap);
	va_end(ap);
	fputc('\n', logf);
	fflush(logf);
}

static void
trace_step(const char *what)
{
	logmsg("rwl_init: %s", what);
}

static int
stats_text(char *buf, size_t len)
{
	struct rwl_stats s;

	rwl_get_stats(&s);
	return snprintf(buf, len,
		"polls=%u null=%u pollword=%u messages=%u other=%u internet_events=%u "
		"ready_wakeups=%u empty_wakeups=%u ticks=%u max_tick_late_ms=%ld "
		"accepts=%u bytes_echoed=%u\n",
		s.polls, s.null_events, s.pollword_events, s.messages, s.other_events,
		s.internet_events, ready_wakeups, empty_wakeups, ticks,
		max_tick_late_ms, accepts, bytes_echoed);
}

static void
drop_client(int i)
{
	close(clients[i]);
	clients[i] = -1;
}

/* Returns 1 if a client asked to quit. */
static int
service_sockets(void)
{
	fd_set rf;
	struct timeval tv = { 0, 0 };
	int i, maxfd = listener, n, quit = 0;

	FD_ZERO(&rf);
	FD_SET(listener, &rf);
	for (i = 0; i < MAX_CLIENTS; i++)
		if (clients[i] >= 0) {
			FD_SET(clients[i], &rf);
			if (clients[i] > maxfd)
				maxfd = clients[i];
		}
	n = select(maxfd + 1, &rf, NULL, NULL, &tv);
	if (n <= 0) {
		empty_wakeups++;
		return 0;
	}
	ready_wakeups++;

	if (FD_ISSET(listener, &rf)) {
		int c = accept(listener, NULL, NULL);

		if (c >= 0) {
			for (i = 0; i < MAX_CLIENTS && clients[i] >= 0; i++)
				;
			if (i == MAX_CLIENTS || rwl_watch_socket(c) < 0) {
				close(c);
			} else {
				clients[i] = c;
				accepts++;
				logmsg("accepted client %d (fd %d)", i, c);
			}
		}
	}
	for (i = 0; i < MAX_CLIENTS; i++) {
		char buf[4096];
		int got;

		if (clients[i] < 0 || !FD_ISSET(clients[i], &rf))
			continue;
		got = read(clients[i], buf, sizeof buf);
		if (got <= 0) {
			if (got < 0 && errno == EWOULDBLOCK)
				continue;
			logmsg("client %d closed", i);
			drop_client(i);
			continue;
		}
		if (got >= 5 && !memcmp(buf, "quit\n", 5)) {
			quit = 1;
		} else if (got >= 6 && !memcmp(buf, "stats\n", 6)) {
			char reply[512];
			int len = stats_text(reply, sizeof reply);

			(void) write(clients[i], reply, len);
		} else {
			/* Echo, waiting for the socket to drain if it fills: a slow
			   client must not lose data. */
			int off = 0;

			while (off < got) {
				int w = write(clients[i], buf + off, got - off);

				if (w > 0) {
					off += w;
					bytes_echoed += w;
				} else if (w < 0 && errno == EWOULDBLOCK) {
					if (rwl_wait(10) == RWL_QUIT)
						return 1;
				} else {
					drop_client(i);
					break;
				}
			}
		}
	}
	return quit;
}

int
main(int argc, char **argv)
{
	struct sockaddr_in a;
	const char *why = "";
	long next_tick;
	int i, one = 1;
	char line[512];

	logf = fopen(argc > 1 ? argv[1] : "evlog", "w");
	for (i = 0; i < MAX_CLIENTS; i++)
		clients[i] = -1;

	logmsg("started");
	rwl_set_trace(trace_step);
	listener = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons(PORT);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (listener < 0 || bind(listener, (struct sockaddr *) &a, sizeof a) < 0 ||
	    listen(listener, 5) < 0) {
		logmsg("cannot listen on %d: %s", PORT, strerror(errno));
		return 1;
	}
	logmsg("listening socket ready");
	if (rwl_init("X loop test", &why) < 0) {
		logmsg("rwl_init failed: %s", why);
		return 1;
	}
	if (rwl_watch_socket(listener) < 0) {
		logmsg("cannot make the listener async: %s", strerror(errno));
		return 1;
	}
	logmsg("listening on %d", PORT);

	next_tick = rwl_now_ms() + TICK_MS;
	for (;;) {
		long now = rwl_now_ms();
		enum rwl_result r;

		if (now - next_tick >= 0) {
			long late = now - next_tick;

			if (late > max_tick_late_ms)
				max_tick_late_ms = late;
			ticks++;
			next_tick += TICK_MS;
			if (next_tick - now <= 0)
				next_tick = now + TICK_MS;
		}
		r = rwl_wait((int) (next_tick - rwl_now_ms() > 0 ? next_tick - rwl_now_ms() : 0));
		if (r == RWL_QUIT) {
			logmsg("quit requested by the Wimp");
			break;
		}
		if (r == RWL_ERROR) {
			logmsg("rwl_wait failed");
			break;
		}
		if (r == RWL_READY && service_sockets()) {
			logmsg("quit requested by a client");
			break;
		}
	}

	stats_text(line, sizeof line);
	logmsg("final: %s", line);
	for (i = 0; i < MAX_CLIENTS; i++)
		if (clients[i] >= 0)
			drop_client(i);
	close(listener);
	rwl_shutdown();
	return 0;
}
