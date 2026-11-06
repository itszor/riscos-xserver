# RISC OS X server port: porting notes

Every workaround or patch needed to build for RISC OS 3.70 with GCCSDK 4.7.4.

| Component | Problem | Fix |
|---|---|---|
| UnixLib (runtime) | Needs SharedUnixLibrary >= 1.07, not on a RISC OS 3.70 machine | Built SUL 1.16 from GCCSDK git `625a8f03` (Jan 2021, matching the Docker image's UnixLib) |
| UnixLib `setitimer` | Returns ENOSYS whenever the program is a Wimp task (`time/setitmr.c:100`), including in a TaskWindow | Don't rely on SIGALRM; drive timers from our own Wimp_Poll loop |
| UnixLib `socketpair` | Treats EINPROGRESS from the non-blocking connect as failure; Internet 5.02 always returns it | Avoid socketpair, or fix UnixLib (`netlib/ul_socketpair.c`) |
| Running from HostFS | `HostFS::HostFS.$.XDev.prog` made UnixLib ask ADFS for a disc called "HostFS" | Run with the CSD set (`Dir HostFS::HostFS.$.XDev` then `Run prog`) |
| libXfont2 | `O_NOFOLLOW` missing from UnixLib | `-DO_NOFOLLOW=0` (no symlinks on RISC OS) |
| SHA-1 | xorg-server needs a SHA-1 library | Own minimal libsha1 (`libsha1/`), tested against FIPS 180 vectors |
| UnixLib `assert.h` | Declares `extern void assert(int)` unguarded, which breaks when `assert` is already a macro on first inclusion | libXfont2: don't build its test utilities. Watch for this in the server |
| xorg-server configure | Requires the `xkbfile` pkg-config module, though the server uses its own `xkb/xkbfile.h` and links nothing from it | Stub `prefix/lib/pkgconfig/xkbfile.pc` with no libs |
| xorg-server | `O_NOFOLLOW` and `SI_USER` missing from UnixLib | `compat/riscos-compat.h`, force-included with `-include` |
| xorg-server XKB | Includes `X11/extensions/XKM.h` / `XKMformat.h` from libxkbfile | Copied those two headers from libxkbfile 1.1.0 into the prefix (header-only) |
| xorg-server lock file | `LockServer` uses `link()` (RISC OS has none); `-nolock` is root-only | `os/utils.c`: `nolock = TRUE` on RISC OS |
| xorg-server XKB | Runs external `xkbcomp` to compile the keymap | `xkb/ddxLoad.c`: load `<xkbdir>/precompiled/<layout>.xkm` (made on the build host by `xkb/make-xkm.sh`) |
| UnixLib `poll` | Leaves `revents` stale when `select` times out (`unix/poll.c`) | Harmless for the X server (ospoll only reads revents after a positive return); fix upstream |
| UnixLib `link` | Not available (the lock file needs it) | See lock file row |
| Running as a Wimp task | The server must not block in `poll()`; the desktop would stop | `os/ospoll.c`: `riscos_wait()` checks sockets with a zero-timeout poll, then sleeps in `rwl_wait()` (evloop/) until the Internet event, a timer or Message_Quit; yields to the Wimp at least every 20 ms when busy; sleeps at most 10 ms while output is blocked. `ospoll_add` makes each socket async. Falls back to plain `poll()` outside the desktop |
| Smart scheduler | Uses `setitimer`, which UnixLib refuses in a Wimp task (silently, as the server doesn't check) | `dix/dispatch.c`: signal-driven scheduling off by default on RISC OS (as `-dumbSched`). Configuring without `HAVE_SETITIMER` instead doesn't link: `os/utils.c` still references the flag |
| Starting the server | `*WimpTask` gives the new task the default "next" slot, too small for the 2.7 MB image ("No writable memory at this address") | `guest/RunXvfb` starts `guest/StartXvfb` as the task, which sets its own `WimpSlot` first, as a `!Run` file does |
| Xriscos build | A new kdrive server needs configure.ac and Makefile.am changes | `--enable-xriscos` in `configure.ac`, `hw/kdrive/riscos/`; `build-xserver.sh` runs `autoreconf` when they change (the image needs `xfonts-utils` for `fontutil.m4`) |
| kdrive server resets | kdrive calls the card and screen functions again on every reset | Xriscos keeps its window and sprite in static state, made once |

## Not a porting problem: rpcemu-extended network wedge (root cause found)

Large transfers between host and guest (e.g. `xwd -root` from the host, about
600 KB from the guest) permanently stop the guest's networking. Cause, confirmed
on "My RiscPC" (RISC OS 3.70, MbufManager 0.17):

1. The guest driver (`riscos-progs/EtherRPCEm/etherrpcem.s`, `receive_frames`)
   acknowledges the interrupt first, then allocates a single 1500-byte mbuf
   (`alloc_s`) for every frame, even a 54-byte ACK. During a big send the
   stack's mbufs are held by unacknowledged data, the allocation fails, and the
   driver takes `rx_exhausted`: "try again on the next interrupt".
2. The emulator (`src/network-nat.c`) only raises the interrupt when a frame
   lands in an empty slot (`rx_deliver`) or after the guest takes one
   (`deliver_queued_packet`). With the slot full and not taken, no interrupt
   ever comes, the 32-frame queue fills, `slirp_can_output()` goes false, and
   SLiRP stops producing frames (netcap shows none).

Evidence: after the wedge the driver workspace shows `data_mbuf=0` and frozen
counters. Raising the interrupt by hand (`SYS &56AC4,3,b%,1`) lets ~11 frames
through each time. Raising it about once a second during an `xwd` lets the
whole 615 KB transfer complete and leaves the network healthy.

Tools: `tools/etherws.sh <workspace>` (driver counters; workspace from
`*Modules`), `XDev.kickirq` on the guest's HostFS.
