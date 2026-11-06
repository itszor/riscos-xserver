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
	RWL_EVENT = 4,		/* the event handler reported work for the caller */
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
   or -1 with a message (static storage) in *why. A second call does nothing
   and returns 0. */
int rwl_init(const char *task_name, const char **why);

/* The Wimp task handle, or 0 before rwl_init(). */
int rwl_task_handle(void);

/*
 * Wimp events for the caller. Every reason rwl does not consume itself goes to
 * the handler: redraw, open, close, pointer, mouse, keys, caret, and messages
 * (17-19, after rwl has checked them for Message_Quit). Null events (reason 0)
 * go to it only while a null interval is set. block is Wimp_Poll's block.
 * The handler returns non-zero when it has queued work for the caller, which
 * makes rwl_wait() return RWL_EVENT.
 */
typedef int (*rwl_handler)(int reason, int *block, void *data);
void rwl_set_handler(rwl_handler fn, void *data);

/* Ask for a null event at least every ms milliseconds (0: none), for polling
   that has no event of its own, such as tracking the pointer. */
void rwl_set_null_interval(int ms);

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
