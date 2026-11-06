@ Vector handlers for the RISC OS X server's Wimp_Poll loop (rwl.c).
@
@ Copied into the RMA by rwl_init(), so they must be position independent and
@ must not refer to anything outside their own block. R12 points at the block:
@
@   +0   pollword      set non-zero when there is something to look at
@   +4   event count   Internet events, for diagnostics
@   +8   last socket   R2 of the last Internet event
@   +12  key capture   bit 0 set: queue key events (the task has the caret)
@   +16  ring write    next key ring slot to fill (written here only)
@   +20  ring read     next key ring slot to take (written by rwl.c only)
@   +24  key overflow  key events dropped because the ring was full
@   +28  key ring      32 words: low-level key number | 0x100 if pressed
@   +156 code          (this file, from rwl_event_code)
@
@ Every register is preserved and both events are passed on, so RISC OS and
@ the rest of the desktop see them as usual.

	.text
	.global	rwl_event_code
	.global	rwl_key_code
	.global	rwl_event_code_end

	Event_Internet = 19
	KEY_RING_SLOTS = 32

@ EventV. Entered in IRQ or SVC mode with interrupts disabled, R0 = event
@ number. Event 19 is the Internet event: R1 = 1 (data or connection waiting
@ on an async socket), 2 (urgent data) or 3 (connection broken), R2 = socket.
rwl_event_code:
	teq	r0, #Event_Internet
	movne	pc, lr
	stmfd	sp!, {r0}
	ldr	r0, [r12, #4]
	add	r0, r0, #1
	str	r0, [r12, #4]
	str	r2, [r12, #8]
	mov	r0, #1
	str	r0, [r12, #0]
	ldmfd	sp!, {r0}
	mov	pc, lr

@ KeyV. Entered in IRQ mode with interrupts disabled. R0 = 1 key released,
@ 2 key pressed (other reasons are passed straight on), R1 = low-level key
@ number. Queued only while capture is on; the ring has one writer (this) and
@ one reader (rwl.c), so no locking is needed.
rwl_key_code:
	cmp	r0, #1
	cmpne	r0, #2
	movne	pc, lr
	stmfd	sp!, {r0-r4}
	ldr	r2, [r12, #12]
	tst	r2, #1
	beq	key_done
	ldr	r2, [r12, #16]			@ write index
	add	r3, r2, #1
	and	r3, r3, #KEY_RING_SLOTS - 1
	ldr	r4, [r12, #20]			@ read index
	cmp	r3, r4
	beq	key_full
	and	r4, r1, #0xff			@ key number
	cmp	r0, #2
	orreq	r4, r4, #0x100			@ pressed
	add	r2, r12, r2, lsl #2
	str	r4, [r2, #28]
	str	r3, [r12, #16]
	mov	r4, #1
	str	r4, [r12, #0]			@ wake the task
key_done:
	ldmfd	sp!, {r0-r4}
	mov	pc, lr
key_full:
	ldr	r2, [r12, #24]
	add	r2, r2, #1
	str	r2, [r12, #24]
	b	key_done
rwl_event_code_end:
