@ EventV handler for the RISC OS X server's Wimp_Poll loop (rwl.c).
@
@ Copied into the RMA by rwl_init(), so it must be position independent and
@ must not refer to anything outside its own block. R12 points at the block:
@
@   +0  pollword      set non-zero on any Internet event; Wimp_Poll watches it
@   +4  event count   for diagnostics
@   +8  last socket   R2 of the last Internet event
@   +12 code          (this file, from rwl_event_code)
@
@ Entered in IRQ or SVC mode with interrupts disabled, R0 = event number.
@ Event 19 is the Internet event: R1 = 1 (data or connection waiting on an
@ async socket), 2 (urgent data) or 3 (connection broken), R2 = socket handle.
@ Every register is preserved and the event is passed on.

	.text
	.global	rwl_event_code
	.global	rwl_event_code_end

	Event_Internet = 19

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
rwl_event_code_end:
