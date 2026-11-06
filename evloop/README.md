# rwl: the Wimp_Poll event loop for the RISC OS X server

The X server has to be a co-operative Wimp task, so it can't block in
`select()`/`poll()`. `rwl` sleeps in `Wimp_Poll` instead:

- `rwl_event.s` is an `EventV` handler, copied into the RMA. On the Internet
  event (19), which the Internet module raises for sockets in async mode
  (`FIOASYNC`), it sets a pollword. Because it lives in the RMA, a program that
  dies without cleaning up leaves a harmless handler behind, not a vector
  pointing at freed memory.
- `rwl_wait(timeout_ms)` calls `Wimp_Poll` with the pollword (reason 13), or
  `Wimp_PollIdle` when a timer is due. It returns `RWL_READY` (some socket may
  be ready; check them all with a zero-timeout `select`/`poll`), `RWL_TIMEOUT`
  or `RWL_QUIT` (Message_Quit). The pollword is cleared before returning, so
  an event that arrives while the caller is working makes the next wait return
  at once.
- `rwl_shutdown()` (also run at exit) closes the task, disables the event and
  releases the vector. The RMA block is freed only after a successful release.

`evtest.c` is a TCP echo server on port 6000 built on it, with a 1-second timer
standing in for X's timers. `hosttest.py` drives it from the host through the
6100 → 6000 NAT forward.

## Results on "My RiscPC" (RISC OS 3.70, emulated StrongARM), 2026-11-06

| Check | Result |
|---|---|
| Idle for 10 s | 12 Wimp polls, 10 timer ticks: sleeps between timers, no busy polling |
| Timer accuracy | max lateness 0 ms (at 10 ms resolution) |
| Round trip, 32 bytes, host to guest via NAT | median 1.32 ms, p95 1.51 ms, max 2.11 ms |
| Echo 1 MB | 0.25 s, about 4 MB/s each way, data intact |
| Two clients at once | both served |
| Quit by client, and by Message_Quit (`XDev.quitx`) | clean exit |
| Second run after the first | works: the vector and RMA block were released |

Timings are emulated, so they say nothing about a real 233 MHz StrongARM.

## Running it

```
WimpTask Run evtest evlog      (from XDev, after the desktop is fully up)
python3 hosttest.py            (on the host)
```

- Start it only once the desktop has finished starting (`tools/waitdesk.sh`).
  `Inet$Started` is set before the desktop exists, and a Wimp task started
  then stopped the boot sequence and hung HostCmd.
- Output to a HostFS file may not appear until the file is closed, even after
  `fflush`, so query a running task over its socket (`stats`).

## Next: into the X server

`os/ospoll.c` / `os/WaitFor.c`: make `ospoll_wait()` call `rwl_wait(timeout)`
and then the existing `poll()` with a zero timeout; call `rwl_watch_socket()`
wherever the server adds an fd (the listener in `os/connection.c`, each client
in `AllocNewConnection`); `rwl_init()` early in `OsInit`. Start the server with
`-dumbSched`, since `setitimer` is refused in Wimp tasks.
