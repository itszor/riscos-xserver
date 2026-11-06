/*
 * rwl: the RISC OS Wimp loop for the X server.
 *
 * The server must be a co-operative Wimp task, so it cannot block in select()
 * or poll() the way it does elsewhere. Instead it sleeps in Wimp_Poll, woken by
 * a pollword that an EventV handler sets when the Internet module raises its
 * event for an async socket, and by Wimp_PollIdle when its next timer is due.
 *
 * Use: rwl_init() once; rwl_watch_socket() on every socket (the listener and
 * each client); then loop on rwl_wait(). A return of RWL_READY means some
 * watched socket may be ready: check them all with a zero-timeout select() or
 * poll(), which is what the X server's ospoll does anyway.
 */
#ifndef RWL_H
#define RWL_H

enum rwl_result {
	RWL_READY = 1,		/* a socket event arrived since the last wait */
	RWL_TIMEOUT = 2,	/* the timeout passed with no socket event */
	RWL_QUIT = 3,		/* the Wimp asked the task to quit */
	RWL_ERROR = -1
};

struct rwl_stats {
	unsigned polls;		/* Wimp_Poll / Wimp_PollIdle calls */
	unsigned null_events;	/* reason 0 */
	unsigned pollword_events;	/* reason 13 */
	unsigned messages;	/* reasons 17, 18 */
	unsigned other_events;
	unsigned internet_events;	/* counted by the EventV handler */
	int last_socket;	/* RISC OS handle from the last Internet event */
};

/* Start the Wimp task and install the event handler. Returns 0 on success,
   or -1 with a message (static storage) in *why. */
int rwl_init(const char *task_name, const char **why);

/* Put a socket in async, non-blocking mode so it wakes rwl_wait(). */
int rwl_watch_socket(int fd);

/* Yield to the Wimp until a watched socket has an event, timeout_ms passes
   (-1: no timeout; 0: yield once and return), or the task is told to quit. */
enum rwl_result rwl_wait(int timeout_ms);

/* Monotonic time in milliseconds (centisecond resolution). */
long rwl_now_ms(void);

void rwl_get_stats(struct rwl_stats *s);

/* Optional progress messages from rwl_init(), for debugging start-up. */
void rwl_set_trace(void (*fn)(const char *what));

/* Remove the handler and close the task. Safe to call twice; also run at
   exit. */
void rwl_shutdown(void);

#endif
