/*
 * rwl: the RISC OS Wimp loop for the X server. See rwl.h.
 *
 * Target: RISC OS 3.70 (26-bit), GCCSDK 4.7.4 / UnixLib.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <kernel.h>
#include <swis.h>

#include "rwl.h"

/* Assembled into this program and copied to the RMA (rwl_event.s). */
extern const char rwl_event_code[], rwl_key_code[], rwl_event_code_end[];

#define EventV			0x10
#define KeyV			0x13
#define Event_Internet		19
#define OSByte_DisableEvent	13
#define OSByte_EnableEvent	14
#define OSModule_Claim		6
#define OSModule_Free		7
#define TASK_WORD		0x4b534154	/* "TASK" */

/* Wimp_Poll mask bits and reason codes */
#define POLL_MASK_NULL		(1u << 0)
#define POLL_SCAN_POLLWORD	(1u << 22)
#define POLL_POLLWORD_HIGH	(1u << 23)
#define REASON_NULL		0
#define REASON_POLLWORD		13
#define REASON_MESSAGE		17
#define REASON_MESSAGE_ACK	18
#define REASON_MESSAGE_ACK2	19
#define Message_Quit		0

/* The RMA block: pollword, event count, last socket, then the code. */
#define BLK_POLLWORD	0
#define BLK_EVENTS	1
#define BLK_LASTSOCK	2
#define BLK_CAPTURE	3	/* bit 0: queue key events */
#define BLK_RING_W	4	/* written by the KeyV handler */
#define BLK_RING_R	5	/* written here */
#define BLK_OVERFLOW	6
#define BLK_RING	7	/* RING_SLOTS words */
#define RING_SLOTS	32
#define BLK_CODE_OFF	((BLK_RING + RING_SLOTS) * 4)	/* 156, as rwl_event.s */
#define KEY_CODE_OFF	(BLK_CODE_OFF + (int) (rwl_key_code - rwl_event_code))

static volatile int *blk;		/* RMA block, or NULL */
static int vector_claimed, key_vector_claimed, event_enabled, task_handle,
    quit_seen, at_exit_set;
static int poll_block[64];
static struct rwl_stats st;
static rwl_handler handler;
static void *handler_data;
static int null_interval_ms;
static void (*trace_fn)(const char *);
static void (*idle_fn)(int idle);

void
rwl_set_trace(void (*fn)(const char *what))
{
	trace_fn = fn;
}

#define TRACE(what) do { if (trace_fn) trace_fn(what); } while (0)

long
rwl_now_ms(void)
{
	int cs = 0;

	_swix(OS_ReadMonotonicTime, _OUT(0), &cs);
	return (long) cs * 10;
}

void
rwl_shutdown(void)
{
	if (task_handle != 0) {
		_swix(Wimp_CloseDown, _INR(0, 1), task_handle, TASK_WORD);
		task_handle = 0;
	}
	if (event_enabled) {
		_swix(OS_Byte, _INR(0, 1), OSByte_DisableEvent, Event_Internet);
		event_enabled = 0;
	}
	/* Free the block only once no vector points into it. If a release
	   fails, leave it: a stray handler that writes to its own block is
	   harmless, one in freed memory is not. */
	if (key_vector_claimed) {
		blk[BLK_CAPTURE] = 0;
		if (_swix(OS_Release, _INR(0, 2), KeyV,
			  (int) blk + KEY_CODE_OFF, (int) blk) != NULL)
			return;
		key_vector_claimed = 0;
	}
	if (vector_claimed) {
		if (_swix(OS_Release, _INR(0, 2), EventV,
			  (int) blk + BLK_CODE_OFF, (int) blk) != NULL)
			return;
		vector_claimed = 0;
	}
	if (blk != NULL) {
		_swix(OS_Module, _INR(0, 2), OSModule_Free, 0, (int) blk);
		blk = NULL;
	}
}

int
rwl_task_handle(void)
{
	return task_handle;
}

void
rwl_set_handler(rwl_handler fn, void *data)
{
	handler = fn;
	handler_data = data;
}

void
rwl_set_idle_hook(void (*fn)(int idle))
{
	idle_fn = fn;
}

void
rwl_set_null_interval(int ms)
{
	null_interval_ms = ms > 0 ? ms : 0;
}

int
rwl_init(const char *task_name, const char **why)
{
	static const int messages[] = { 0 };	/* Message_Quit is always sent */
	size_t code_len = (size_t) (rwl_event_code_end - rwl_event_code);
	void *mem = NULL;
	_kernel_oserror *e;

	if (task_handle != 0)
		return 0;
	if (!at_exit_set) {
		atexit(rwl_shutdown);
		at_exit_set = 1;
	}

	TRACE("claiming RMA");
	if ((e = _swix(OS_Module, _IN(0) | _IN(3) | _OUT(2), OSModule_Claim,
		       (int) (BLK_CODE_OFF + code_len), &mem)) != NULL) {
		*why = e->errmess;
		return -1;
	}
	blk = mem;
	blk[BLK_POLLWORD] = 0;
	blk[BLK_EVENTS] = 0;
	blk[BLK_LASTSOCK] = -1;
	blk[BLK_CAPTURE] = 0;
	blk[BLK_RING_W] = 0;
	blk[BLK_RING_R] = 0;
	blk[BLK_OVERFLOW] = 0;
	memcpy((char *) mem + BLK_CODE_OFF, rwl_event_code, code_len);
	TRACE("syncing code");
	/* StrongARM has separate caches: the new code must reach the I-cache. */
	_swix(OS_SynchroniseCodeAreas, _INR(0, 2), 1, (int) mem + BLK_CODE_OFF,
	      (int) mem + BLK_CODE_OFF + (int) code_len - 4);

	TRACE("claiming EventV");
	if ((e = _swix(OS_Claim, _INR(0, 2), EventV, (int) mem + BLK_CODE_OFF,
		       (int) mem)) != NULL) {
		*why = e->errmess;
		rwl_shutdown();
		return -1;
	}
	vector_claimed = 1;

	TRACE("claiming KeyV");
	if ((e = _swix(OS_Claim, _INR(0, 2), KeyV, (int) mem + KEY_CODE_OFF,
		       (int) mem)) != NULL) {
		*why = e->errmess;
		rwl_shutdown();
		return -1;
	}
	key_vector_claimed = 1;

	TRACE("enabling event 19");
	_swix(OS_Byte, _INR(0, 1), OSByte_EnableEvent, Event_Internet);
	event_enabled = 1;

	TRACE("Wimp_Initialise");
	if ((e = _swix(Wimp_Initialise, _INR(0, 3) | _OUT(1), 310, TASK_WORD,
		       task_name, messages, &task_handle)) != NULL) {
		*why = e->errmess;
		rwl_shutdown();
		return -1;
	}
	TRACE("initialised");
	return 0;
}

int
rwl_watch_socket(int fd)
{
	int on = 1;

	if (ioctl(fd, FIONBIO, &on) < 0)
		return -1;
	return ioctl(fd, FIOASYNC, &on);
}

/* One Wimp_Poll or Wimp_PollIdle. Returns the reason code; *woken is set
   if the event handler reported work. */
static int
poll_once(int with_null, long deadline_ms, int use_deadline, int *woken)
{
	/* Not POLL_POLLWORD_HIGH: it puts the pollword ahead of redraws, and
	   under steady network traffic (a client drawing all the time) the
	   pollword is nearly always set, so windows went unredrawn until the
	   traffic stopped. The pollword is looked at after every poll anyway. */
	unsigned mask = POLL_SCAN_POLLWORD;
	int reason = -1;

	if (!with_null && !use_deadline)
		mask |= POLL_MASK_NULL;
	st.polls++;
	if (idle_fn != NULL)
		idle_fn(1);
	if (use_deadline) {
		_swix(Wimp_PollIdle, _INR(0, 3) | _OUT(0), mask, poll_block,
		      (int) (deadline_ms / 10), (int) blk, &reason);
	} else {
		_swix(Wimp_Poll, _IN(0) | _IN(1) | _IN(3) | _OUT(0), mask,
		      poll_block, (int) blk, &reason);
	}
	if (idle_fn != NULL)
		idle_fn(0);
	switch (reason) {
	case REASON_NULL:
		st.null_events++;
		if (null_interval_ms > 0 && handler != NULL &&
		    handler(reason, poll_block, handler_data))
			*woken = 1;
		break;
	case REASON_POLLWORD:
		st.pollword_events++;
		break;
	case REASON_MESSAGE:
	case REASON_MESSAGE_ACK:
	case REASON_MESSAGE_ACK2:
		st.messages++;
		if (poll_block[4] == Message_Quit)
			quit_seen = 1;
		else if (handler != NULL && handler(reason, poll_block, handler_data))
			*woken = 1;
		break;
	default:
		st.other_events++;
		if (handler != NULL && handler(reason, poll_block, handler_data))
			*woken = 1;
		break;
	}
	return reason;
}

/* Take the pollword. Cleared before the caller looks at its sockets, so an
   event arriving while it does sets it again and the next wait returns at
   once rather than missing it. */
static int
take_pollword(void)
{
	if (blk[BLK_POLLWORD] == 0)
		return 0;
	blk[BLK_POLLWORD] = 0;
	/* The KeyV handler sets the pollword too: hand any keys over. */
	if (blk[BLK_RING_R] != blk[BLK_RING_W] && handler != NULL)
		(void) handler(RWL_REASON_KEYS, NULL, handler_data);
	return 1;
}

void
rwl_key_capture(int on)
{
	if (blk != NULL)
		blk[BLK_CAPTURE] = on ? 1 : 0;
}

int
rwl_key_next(int *key, int *pressed)
{
	int r;

	if (blk == NULL)
		return 0;
	r = blk[BLK_RING_R];
	if (r == blk[BLK_RING_W])
		return 0;
	*key = blk[BLK_RING + r] & 0xff;
	*pressed = (blk[BLK_RING + r] >> 8) & 1;
	blk[BLK_RING_R] = (r + 1) & (RING_SLOTS - 1);
	return 1;
}

unsigned
rwl_key_overflows(void)
{
	return blk != NULL ? (unsigned) blk[BLK_OVERFLOW] : 0;
}

enum rwl_result
rwl_wait(int timeout_ms)
{
	long now = rwl_now_ms();
	long deadline = now + (timeout_ms > 0 ? timeout_ms : 0);
	int woken = 0;

	if (blk == NULL || task_handle == 0)
		return RWL_ERROR;

	/* Work already waiting, or a pure yield: one Wimp_Poll with null
	   events allowed, so other tasks get a turn but we are back at once. */
	if (blk[BLK_POLLWORD] != 0 || timeout_ms == 0) {
		poll_once(1, 0, 0, &woken);
		if (quit_seen)
			return RWL_QUIT;
		if (take_pollword())
			return RWL_READY;
		return woken ? RWL_EVENT : RWL_TIMEOUT;
	}

	for (;;) {
		/* Wake for the caller's timeout or the next null event the
		   handler asked for, whichever is sooner. */
		int use_deadline = timeout_ms > 0 || null_interval_ms > 0;
		long wake = deadline;

		if (null_interval_ms > 0) {
			long next_null = rwl_now_ms() + null_interval_ms;

			if (timeout_ms <= 0 || next_null - deadline < 0)
				wake = next_null;
		}
		poll_once(0, wake, use_deadline, &woken);
		if (quit_seen)
			return RWL_QUIT;
		if (take_pollword())
			return RWL_READY;
		if (woken)
			return RWL_EVENT;
		if (timeout_ms > 0 && rwl_now_ms() - deadline >= 0)
			return RWL_TIMEOUT;
	}
}

void
rwl_get_stats(struct rwl_stats *s)
{
	*s = st;
	if (blk != NULL) {
		s->internet_events = (unsigned) blk[BLK_EVENTS];
		s->last_socket = blk[BLK_LASTSOCK];
	}
}
