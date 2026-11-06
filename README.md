# RISC OS X server

Target: RISC OS 3.70 on a StrongARM Risc PC ("My RiscPC" in rpcemu-extended).
Toolchain: GCCSDK 4.7.4 Release 5 (Docker image `riscosdotinfo/riscos-gccsdk-4.7`,
built Feb 2021) plus host tools, as image `xdev-gccsdk:1` (`docker/Dockerfile`).

## Results

**Phase 0 (toolchain on RISC OS 3.70): works.** `phase0/smoke.c` passes uname,
a 12 MB heap, `select` timeouts, TCP listen/connect/accept/send/recv on
loopback, name lookup and file I/O. Needs SharedUnixLibrary (built here as
1.16) but not CallASWI: RISC OS 3.70 has `OS_CallASWIR12` natively.
`setitimer` is refused inside any Wimp task (UnixLib design) and `socketpair`
is broken against Internet 5.02 (UnixLib bug). See `PORTING-NOTES.md`.

**Phase 1 (Xvfb from xorg-server 1.19.7): works.**
- Static AIF, 2.7 MB. About 5.7 MB of RAM in use at 640x480x16.
- Serves Linux clients over TCP: `xdpyinfo`, `xlogo`, `xeyes` (SHAPE),
  `xclock`, `xterm` with the built-in `fixed` font render correctly
  (checked by reading the framebuffer back inside the guest).
- 20 extensions, including RENDER, Composite, DAMAGE, XFIXES, RANDR, XKB, XTEST.
- Full-screen `GetImage` (614,400 bytes) over guest loopback: 0.08-0.11 s.
  Emulated timing, so not a measure of a real 233 MHz StrongARM.
- Source changes to xorg-server: two small `#ifdef __riscos__` patches
  (precompiled XKB keymaps; no lock file). See `patches/`.

**Blocking issue found, in the emulator, not the server:** large transfers
through rpcemu-extended's NAT port-forward wedge it. Details in
`PORTING-NOTES.md`.

## Layout

| Path | What |
|---|---|
| `fetch-sources.sh` | Downloads the upstream tarballs, checks `sources/SHA256SUMS`, unpacks into `build/` and applies `patches/` |
| `docker/Dockerfile` | GCCSDK image plus pkg-config, autotools, util-macros, xkbcomp |
| `build-deps.sh` | Cross-builds zlib, libsha1, xorgproto, xtrans, libXau, libfontenc, libXfont2, pixman into `prefix/` |
| `build-xserver.sh` | Configures and builds xorg-server (Xvfb only for now) |
| `build-sul.sh` | Builds the SharedUnixLibrary module RISC OS 3.70 needs, from a GCCSDK clone |
| `stage-xvfb.sh` | Rebuild, convert to AIF, copy to the machine's HostFS |
| `compat/riscos-compat.h` | Force-included header filling UnixLib gaps |
| `libsha1/` | Minimal SHA-1 with the libsha1 API (tested against FIPS vectors) |
| `xkb/make-xkm.sh` | Precompiles us/gb keymaps (evdev keycodes) on the build host |
| `patches/` | Our changes to xorg-server 1.19.7, as a diff |
| `tools/` | `start-xvfb.sh`, `xping` (in-guest X handshake/GetImage test), VNC screenshot/click/key, CPU sampler |
| `evloop/` | The Wimp_Poll event loop (`rwl`), its test task and results (Phase 2, step 1) |
| `guest/` | RISC OS-side files for the machine's HostFS `XDev` folder: the Xvfb launcher and small BASIC helpers |
| `phase0/` | Toolchain smoke test |
| `sources/SHA256SUMS` | Checksums of the upstream tarballs (the tarballs themselves are not in git) |

## Reproduce

```
./fetch-sources.sh
docker build -t xdev-gccsdk:1 docker
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/xdev xdev-gccsdk:1 /xdev/build-deps.sh
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/xdev xdev-gccsdk:1 /xdev/xkb/make-xkm.sh
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/xdev xdev-gccsdk:1 /xdev/build-xserver.sh
./build-sul.sh                           # SharedULib,ffa into stage/
./stage-xvfb.sh
tools/start-xvfb.sh                      # emulator must be running
DISPLAY=localhost:100 xdpyinfo           # NAT forward host 6100 -> guest 6000
```

`build/`, `prefix/`, `logs/`, `stage/` and the tarballs are generated and not
in git. On the machine, HostFS `XDev` needs `Xvfb`, `SharedULib`, the
`xkb.precompiled` keymaps from `stage/xkb`, and the files in `guest/`.
