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

**Network wedge in the emulator: fixed.** Large transfers used to stop the
guest's networking (a lost receive interrupt in rpcemu-extended). Fixed there
in commit 800f1e4; details in `PORTING-NOTES.md`.

**Phase 2, step 1 (the server as a Wimp task): works.** The server sleeps in
`Wimp_Poll` between requests and keeps the desktop responsive under load.
See `evloop/README.md`.

**Phase 2, step 2 (Xriscos, the X screen in a desktop window): display,
mouse and keyboard work.** See "Xriscos" below.

## Xriscos

A kdrive server (`hw/kdrive/riscos`, built with `--enable-xriscos`). Start it
with `XDev.RunX` (640x480 by default; edit `XDev.StartX` for `-screen WxH` or
`WxHx24`).

- The X screen is a RISC OS sprite, 16bpp (X depth 15) or 32bpp (depth 24),
  which fb draws into directly: the X visual's masks follow RISC OS's pixel
  layout (red in the low bits), so nothing is converted.
- It is shown in an ordinary window (title, close, back, toggle, size).
  Resizing the window resizes the X screen: once the size has held for
  250 ms, a work procedure swaps in a sprite of the new size, resizes the root
  window and tells clients (RandR), as Xephyr does. The window can grow to the
  size of the desktop (or the starting size, if bigger), and shrink to 64x64.
  RISC OS 3.70 draws a vertical scroll bar beside the size icon whatever the
  flags say; it is kept from scrolling.
  The shadow layer's damage becomes `Wimp_ForceRedraw`; redraw requests plot
  the sprite with `OS_SpriteOp` 52, through a ColourTrans table on a desktop
  of 256 colours or fewer.
- While the pointer is over the window it is read every 20 ms: Select, Menu
  and Adjust are X buttons 1, 2 and 3. A click takes the input focus.
- Keys come from a `KeyV` handler in rwl's RMA block: every physical key going
  down or up, by RISC OS low-level key number, modifiers included. They are
  captured only while the window has the caret, mapped to evdev key codes
  (`hw/kdrive/riscos/kbd.c`), and XKB applies the layout. The layout defaults
  from the RISC OS keyboard setting (`OS_Byte` 71: UK gives `gb`, USA `us`);
  `-xkb-layout` overrides it. Losing the caret releases any keys X thinks are
  held. `-keylog` (or `XDev.RunXtest`) logs each key and its mapping.
- The X cursor is the RISC OS pointer (`hw/kdrive/riscos/cursor.c`). Each X
  cursor becomes a 2bpp sprite, and while the pointer is over the window the
  current one is pointer shape 2 (`OS_SpriteOp` 36, with its palette); leaving
  the window or quitting puts the Wimp's `ptr_default` back. Core cursors keep
  their two colours exactly; full-colour (ARGB) ones are reduced to
  transparent, white, black and grey; anything bigger than 32x32 is clipped
  around the hot spot. X no longer draws a cursor into the framebuffer.
- Closing the window, or Message_Quit, shuts the server down.
- The window and sprite are kept across server resets.

Tested on "My RiscPC" (16bpp desktop): `xlogo`, `xeyes`, `xclock -update 1`
and a coloured `xterm` from Linux draw correctly; `xev` sees motion and all
three buttons, each press with its release, at the right coordinates; the
window can be moved and redraws; closing it ends the server and its clients;
it carries on across resets.

Keyboard: `tools/keytest/keytest.py` sends 97 keys over VNC, one per physical
key (letters, digits, punctuation, both ISO keys, F1-F12, editing and cursor
keys, every modifier, the keypad), and checks `xev` gets each with the X
keycode the emulator's own VNC table gives that key: all 97 correct, each with
its release. Typed into an `xterm` with the `gb` layout, `|`, `\` and `£`
come out right. Taking the caret away while Shift is held releases Shift in
X, and the real release is not delivered twice.

Resizing: dragging the size icon took the X screen from 640x480 to 440x343
and then 719x519, with the title, `xdpyinfo`, `xrandr` and a client polling
`xwininfo -root` all following; the toggle icon gave the full 779x579. Clients
redraw on the exposures; windows left outside a smaller screen are not moved
(there is no window manager).

Pointer: over an `xterm` the pointer is X's I-beam, over the root the X
cursor, and off the window the RISC OS arrow with its own colours, with
nothing drawn into the window. `xsetroot` with a core red-on-yellow `watch`
(`XCURSOR_CORE=1`) shows exactly red and yellow; Adwaita's ARGB arrow at 24
pixels shows black with a white outline, and at 48 pixels is clipped to 32x32
at the tip. Quitting with the pointer over the window restores the arrow.

Not done yet:
- Keys for other windows (F12, hot keys) are not passed on while the X window
  has the caret.
- Only `us` and `gb` keymaps are precompiled; other territories fall back to
  `us`.
- Full-colour cursors are reduced to three colours, and cursors over 32x32
  are clipped.
- X pointer warps (`XWarpPointer`) do not move the RISC OS pointer.
- RandR offers only the current size: clients cannot change it (`xrandr -s`);
  the window decides.
- The window opens over the icon bar on a small desktop.
- Untested on 256-colour and 32bpp desktops.

## Layout

| Path | What |
|---|---|
| `fetch-sources.sh` | Downloads the upstream tarballs, checks `sources/SHA256SUMS`, unpacks into `build/` and applies `patches/` |
| `docker/Dockerfile` | GCCSDK image plus pkg-config, autotools, util-macros, xkbcomp |
| `build-deps.sh` | Cross-builds zlib, libsha1, xorgproto, xtrans, libXau, libfontenc, libXfont2, pixman into `prefix/` |
| `build-xserver.sh` | Configures and builds xorg-server (Xvfb only for now) |
| `build-sul.sh` | Builds the SharedUnixLibrary module RISC OS 3.70 needs, from a GCCSDK clone |
| `stage.sh` | Rebuild, convert Xvfb and Xriscos to AIF, copy them and `guest/` to the machine's HostFS |
| `make-patch.sh` | Regenerate `patches/` from the edited tree in `build/` and check it reproduces it |
| `compat/riscos-compat.h` | Force-included header filling UnixLib gaps |
| `libsha1/` | Minimal SHA-1 with the libsha1 API (tested against FIPS vectors) |
| `xkb/make-xkm.sh` | Precompiles us/gb keymaps (evdev keycodes) on the build host |
| `patches/` | Our changes to xorg-server 1.19.7, including the new `hw/kdrive/riscos` driver, as a diff |
| `tools/keytest/` | Keyboard check: VNC key sender (`--type` too), the emulator's keysym table, and `keytest.py` |
| `tools/` | `start-xvfb.sh`, `xping` (in-guest X handshake/GetImage test), `swicount.sh` (SWIs called from application space: shows busy-waiting), `waitdesk.sh`, VNC screenshot/click/key/mouse, CPU sampler |
| `evloop/` | The Wimp_Poll event loop (`rwl`), its test task and results (Phase 2, step 1) |
| `guest/` | RISC OS-side files for the machine's HostFS `XDev` folder: launchers (`RunX`, `RunXvfb`) and small BASIC helpers |
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
./stage.sh
tools/start-xvfb.sh                      # emulator must be running
DISPLAY=localhost:100 xdpyinfo           # NAT forward host 6100 -> guest 6000
```

`build/`, `prefix/`, `logs/`, `stage/` and the tarballs are generated and not
in git. On the machine, HostFS `XDev` needs `Xvfb`, `SharedULib`, the
`xkb.precompiled` keymaps from `stage/xkb`, and the files in `guest/`.
