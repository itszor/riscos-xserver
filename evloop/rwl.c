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
extern const char rwl_event_code[], rwl_event_code_end[];

#define EventV			0x10
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
#define BLK_CODE_OFF	12

static volatile int *blk;		/* RMA block, or NULL */
static int vector_claimed, event_enabled, task_handle, quit_seen, at_exit_set;
static int poll_block[64];
static struct rwl_stats st;
static void (*trace_fn)(const char *);

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
	if (vector_claimed) {
		/* Free the block only once the vector no longer points into it. If
		   the release fails, leave it: a stray handler that writes to its
		   own block is harmless, one in freed memory is not. */
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
rwl_init(const char *task_name, const char **why)
{
	static const int messages[] = { 0 };	/* Message_Quit is always sent */
	size_t code_len = (size_t) (rwl_event_code_end - rwl_event_code);
	void *mem = NULL;
	_kernel_oserror *e;

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

/* One Wimp_Poll or Wimp_PollIdle. Returns the reason code. */
static int
poll_once(int with_null, long deadline_ms, int use_deadline)
{
	unsigned mask = POLL_SCAN_POLLWORD | POLL_POLLWORD_HIGH;
	int reason = -1;

	if (!with_null && !use_deadline)
		mask |= POLL_MASK_NULL;
	st.polls++;
	if (use_deadline) {
		_swix(Wimp_PollIdle, _INR(0, 3) | _OUT(0), mask, poll_block,
		      (int) (deadline_ms / 10), (int) blk, &reason);
	} else {
		_swix(Wimp_Poll, _IN(0) | _IN(1) | _IN(3) | _OUT(0), mask,
		      poll_block, (int) blk, &reason);
	}
	switch (reason) {
	case REASON_NULL:
		st.null_events++;
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
		break;
	default:
		st.other_events++;
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
	return 1;
}

enum rwl_result
rwl_wait(int timeout_ms)
{
	long deadline = rwl_now_ms() + (timeout_ms > 0 ? timeout_ms : 0);

	if (blk == NULL || task_handle == 0)
		return RWL_ERROR;

	/* Work already waiting, or a pure yield: one Wimp_Poll with null
	   events allowed, so other tasks get a turn but we are back at once. */
	if (blk[BLK_POLLWORD] != 0 || timeout_ms == 0) {
		poll_once(1, 0, 0);
		if (quit_seen)
			return RWL_QUIT;
		return take_pollword() ? RWL_READY : RWL_TIMEOUT;
	}

	for (;;) {
		poll_once(0, deadline, timeout_ms > 0);
		if (quit_seen)
			return RWL_QUIT;
		if (take_pollword())
			return RWL_READY;
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
