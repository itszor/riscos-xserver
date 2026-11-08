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
`WxHx24`). Arguments to `RunX` are passed on to the server, as in
`Obey XDev.RunX -rootless`.

- The X screen is a RISC OS sprite, 16bpp (X depth 15) or 32bpp (depth 24),
  which fb draws into directly: the X visual's masks follow RISC OS's pixel
  layout (red in the low bits), so nothing is converted.
- It is shown in an ordinary window (title, close, back, toggle, size).
  Resizing the window resizes the X screen: once the size has held for
  250 ms, a work procedure swaps in a sprite of the new size, resizes the root
  window and tells clients (RandR: the screen change, the root's
  ConfigureNotify, and the CRTC and output changes), as Xephyr does. The
  window can grow to the
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
- An icon bar icon (the X logo, `tools/mklogo.py` from xlogo's own drawing
  code, as a 34x34 "2x2" sprite in the Wimp's greys, built into the server):
  Select brings the window to the front, or opens it again after it was
  closed; Menu gives Fullscreen (ticked while it is) and Quit. Closing the
  window leaves X running; Quit, or Message_Quit, shuts it down.
- Full screen, from the menu or Alt+Return (which switches back too; X never
  sees that Return): a window with no furniture covers the desktop and X draws
  straight into screen memory at the desktop's size, with no sprite and
  nothing plotted. X's depth must be the desktop's (15 on a 32K-colour
  desktop, 24 on 16M). When the Wimp reports part of that window lost (an
  error box, say), X repaints it, background and Expose events, as for
  uncovered windows. A mode change drops back to the window first, before X
  can draw again. The cursor X thinks is showing is re-sent after each switch.
- While X draws on the screen it is cacheable (`hw/kdrive/riscos/vcache.c`):
  RISC OS 3.70 maps screen memory bufferable but uncached, so every read
  (CopyArea, scrolling) is a slow uncached VRAM access. `OS_Memory` 0 makes
  the screen's pages cacheable as each `Wimp_Poll` returns (rwl's idle hook);
  just before the next one the data cache is cleaned (`OS_SynchroniseCodeAreas`)
  and the pages made uncacheable again, so other tasks' drawing never sits in
  the cache. With AutoVCache loaded, only the cleaning is done. `-novcache`
  turns this off.
- The window and sprite are kept across server resets; so is full screen.
- **Rootless** (icon bar menu, saved in `Choices:Xriscos.Options`, or
  `<Xriscos$Dir>.Options` without a `Choices$Write`; `-rootless` and
  `-norootless` override it for a run): each top-level X window is a RISC OS
  window of its own, and the X screen is the desktop. It takes effect when
  the server next starts (a server can't change between the two while it
  runs). See *Rootless mode*, below.

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

Icon bar and full screen: the icon and menu appear as they should; Fullscreen
(800x600 desktop) gave X an 800x600 screen with `xterm` and `xclock` drawn and
updating straight onto the screen, `xdpyinfo` and RandR events following, and
typing reaching the `xterm`. An error box over it was repainted when it went.
Alt+Return went back to the window at its old place and size, and in again;
the close icon hid the window and Select on the icon brought it back; Quit
ended the server and removed the icon. `*WimpMode` to 640x480 while full
screen put X back in its window. The page table entry for the screen goes
from &FF6 (bufferable) to &FFE (cacheable, bufferable) and back, so
`OS_Memory` does change VRAM pages on RISC OS 3.70; one on/clean/off cycle over
the 235 pages of an 800x600 32K screen took about 150 us in the emulator,
which does not model caches, so its cost on a real StrongARM is still to be
measured.

fvwm: fvwm 2.7 never follows a change of screen size (its RandR handling is
compiled out, `#if 0` in `libs/FScreen.c`); `xev -root` shows the server
sending RRScreenChangeNotify, ConfigureNotify on the root, and the CRTC and
output changes, which RandR 1.2 clients (fvwm3, xfwm4, ...) use.

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
- Full screen needs the desktop at X's depth; X's depth is fixed at start.
- No Info window yet on the icon bar menu.
- Untested on 256-colour and 32bpp desktops.

## Rootless mode

Built on xorg-server's generic rootless layer (`miext/rootless`, as used by
XQuartz and XWin's `-mwextwm`), which gives every mapped top-level window a
"frame" with its own backing buffer. `hw/kdrive/riscos/rootless.c` makes a
frame a Wimp window and a sprite in the X screen's format: fb draws into the
sprite, the layer's damage becomes `Wimp_ForceRedraw` when it flushes, and
redraw requests plot the sprite. The X root window is never shown, and costs
one row of pixels (the layer's `RootlessUpdateScreenPixmap`).

The Wimp is the window manager, on the ARM, so no X window manager runs on
the PC and nothing for one crosses the network:

- Ordinary windows get a title bar (`_NET_WM_NAME` or `WM_NAME`, followed
  through an XACE property hook), back, close and toggle-size icons, and a
  size icon where RISC OS shows one without a scroll bar (3.70 doesn't). A
  drag from the bottom right 8 pixels of a window resizes it, as the size
  icon would (`Wimp_DragBox` type 2), with X's `bottom_right_corner` cursor
  while the pointer is there; X doesn't see a press that starts there;
  override-redirect ones (menus, tooltips) none, and they are kept above
  the others.
- Moving, resizing, raising or lowering a window on the desktop
  reconfigures the X window (`ConfigureWindow`, as XWin does); X's own
  moves and restacking move the Wimp window, and where the Wimp keeps a
  window on screen the X window is moved to match.
- The close icon sends `WM_DELETE_WINDOW` when the client takes it, and
  otherwise disconnects the client, as xkill would.
- Clicking a window brings it to the front and gives it the caret and the X
  input focus. The pointer is reported to X in desktop coordinates while it
  is over an X window (or a button is held), and is X's cursor there.

Found on the way (all fixed here):

- **Render was lost in frames.** The rootless layer points a framed window's
  pixmap at the frame's pixels with its base moved back to the screen's
  origin, but leaves the pixmap the frame's size, and fb bounds the pixman
  image for a picture by the pixmap's size. So Render drew nothing in a
  window away from the top left of the screen, and could write outside the
  frame near it. `fb/fbpict.c` now makes the image reach the window.
- **Render trapezoids and triangles** go straight to pixman in fb, and the
  layer didn't wrap them, so they drew into the hidden root (xeyes' eyes).
  `miext/rootless/rootlessScreen.c` now wraps them, as it does Composite.
- **Flushing:** the layer reports damage with `DamageRects` and asks for it to
  be shown with `UpdateRegion(NULL)` or with `StopDrawing(flush)`, depending
  on whether the frame was being drawn on; both must flush.
- **RISC OS 3.70 shows a window's size icon only beside a scroll bar**, and
  an X window has no use for one, so frames have none, and resize from
  their bottom right corner instead.
- **The layer's scratch pixmap headers went back to the pool dirty.**
  `SetPixmapBaseToScreen` sets `drawable.x` to 1 for a 16bpp frame at an
  odd address, and `ModifyPixmapHeader` doesn't reset it, so the next user
  of the screen's scratch header, Render's `AddGlyphs`, took each glyph from
  one pixel in: LibreOffice's text lost a column, and i and l vanished. The
  layer now clears it (and `screen_x`/`y`) before freeing a header. Its
  headers are also sized to reach the frame in screen coordinates, as dix
  bounds `GetImage` on a window by its pixmap (xwd of a window away from
  the top left failed with BadMatch while it was being drawn on), and
  `RootlessGlyphs` damages the union of the glyphs (it took the max of the
  left and top edges).
- **Composite is off when rootless.** Composite redirects any ARGB window
  (Firefox's popups, for one) and sets its pixmap's `screen_x`/`screen_y`;
  in a frame that pixmap is the layer's scratch header, which went back to
  the pool with the offset and was then used for another frame, so fb drew
  and read that far outside the frame's sprite: corruption, then a data
  abort in `fbBlt` when Firefox scrolled. XQuartz and XWin's multiwindow
  mode go without Composite as well. `rootlessCommon.c` also clears the
  offset whenever it takes a scratch header.

Tested on "My RiscPC" with `-rootless`: `xterm` (core and Xft fonts),
`xclock` (core and `-render`), and `xeyes` in each combination of Render,
Present and Shape each appear as their own window, titled, where the
client put them, and draw and redraw correctly; dragging the title bar moved
the X window to match (40,60 to 100,211), the size icon resized it (to
239x276, xterm redrawing), typing reached the xterm with its title showing
the focus, xterm's Ctrl+click menu opened as an undecorated window, the
close icon closed an xeyes, and scrolling output displayed at once. The
menu item saved the option, the server came up rootless from it, and
unticking it said the change would come at the next start. Later:
dragging a window's bottom right corner resizes it, with X's resize
cursor; LibreOffice 26.2's start centre (from a Linux host) draws its text
as Xvfb does at the same depth; Firefox ran and scrolled until the
Composite fault above, since fixed (not yet re-run).

Rootless limitations:

- Shaped windows are shown whole (RISC OS 3.7 has no shaped windows).
- X only knows where the pointer is while it's over an X window.
- No window placement: windows open where their clients ask (the Wimp keeps
  them on screen). No iconising, and no `_NET_WM` hints.
- A mode change reopens the windows where they were, but the X screen keeps
  its size.
- Each window's sprite is in the server's own memory, about 2 bytes a pixel
  (32bpp: 4), within its WimpSlot.

### Speed: scrolling and redraws

Profiled on "My RiscPC" (sampling the ARM's PC through the debugger) with a
client scrolling a 1200x700 window as fast as it could. Before:

- on an 8bpp desktop, 42% of the time was SpriteExtend plotting the
  window's 16bpp sprite through the 32K-entry colour table, the whole window
  each time, as a scroll was damage to all of it; 28% was X moving the
  sprite's pixels (`memcpy`), which a scroll needs anyway;
- `rwl` polled with a high-priority pollword, which the Wimp reports before
  redraws: under steady traffic it was nearly always set, and the window was
  redrawn 13 times in 5 seconds while the client scrolled 1000 times.

Now:

- **The pollword is ordinary priority** (`evloop/rwl.c`): redraws come
  between bursts of requests, 224-252 in those 5 seconds.
- **Scrolls are copied on screen** with `Wimp_BlockCopy`. The rootless
  layer has a new optional frame procedure, `ScrollFrame`, which
  `RootlessCopyArea` (a plain copy within a frame) and `RootlessCopyWindow`
  (a child window moved within its frame) call after fb has copied the
  pixels; what it takes isn't damaged. `rootless.c` keeps one pending copy
  per frame and does it at the flush, merging a run of scrolls into one
  copy by their summed offset where that is exact and turning anything else
  into damage (always safe: the sprite is current). Before copying it
  redraws wherever a source was still waiting to be redrawn
  (`issued`), so stale pixels are never moved.
- Results (5 s of scrolling, 1360x768 desktop): at 8bpp, 194 scrolls/s
  against 167, with 453 copies and no plotting time against 224 full
  replots taking 33%; at 16bpp, where a replot is a plain copy, the two are
  about even in RPCEmu (VRAM reads may cost more on a real machine).
  `Set Xriscos$NoBlockCopy 1` before starting the server turns the copies
  off, to compare.
- Checked by reading the screen back through the debugger and comparing it
  with the window's pixels from X: identical after 200 rapid scrolls,
  including with another window covering part of it.

Not yet done: a plotter of our own for 8bpp desktops (a 4K-entry table that
fits the StrongARM's 16K data cache); URIconv (from !Boot) polls for null
events all the time, which costs a task switch per poll.

## Layout

| Path | What |
|---|---|
| `setup.sh` | Host setup: checks tools, runs `fetch-sources.sh`, builds the Docker image, clones GCCSDK |
| `build.sh` | Runs the whole build in the image; servers, SharedULib and keymaps end up in `stage/` |
| `fetch-sources.sh` | Downloads the upstream tarballs, checks `sources/SHA256SUMS`, unpacks into `build/` and applies `patches/` |
| `docker/Dockerfile` | GCCSDK image plus pkg-config, autotools, util-macros, xkbcomp |
| `build-deps.sh` | Cross-builds zlib, libsha1, xorgproto, xtrans, libXau, libfontenc, libXfont2, pixman into `prefix/` |
| `build-xserver.sh` | Configures and builds xorg-server (Xvfb only for now) |
| `build-sul.sh` | Builds the SharedUnixLibrary module RISC OS 3.70 needs, from a GCCSDK clone |
| `dist.sh` | Puts together the `!Xriscos` application from `stage/` and `app/` in `dist/`, and zips it with RISC OS filetypes (`tools/riscos-zip.py`) |
| `app/` | `!Xriscos`'s `!Run`, `!Boot` and `!Help` (its `!Sprites` are made by `tools/mklogo.py --app`) |
| `stage.sh` | Rebuild, convert Xvfb and Xriscos to AIF, copy them and `guest/` to the machine's HostFS |
| `make-patch.sh` | Regenerate `patches/` from the edited tree in `build/` and check it reproduces it |
| `compat/riscos-compat.h` | Force-included header filling UnixLib gaps |
| `libsha1/` | Minimal SHA-1 with the libsha1 API (tested against FIPS vectors) |
| `xkb/make-xkm.sh` | Precompiles us/gb keymaps (evdev keycodes) on the build host |
| `patches/` | Our changes to xorg-server 1.19.7, including the new `hw/kdrive/riscos` driver, as a diff |
| `tools/keytest/` | Keyboard check: VNC key sender (`--type` too), the emulator's keysym table, and `keytest.py` |
| `tools/` | `start-xvfb.sh`, `xping` (in-guest X handshake/GetImage test), `swicount.sh` (SWIs called from application space: shows busy-waiting), `waitdesk.sh`, VNC screenshot/click/key/mouse, CPU sampler, `mklogo.py` (the icon bar sprite, `hw/kdrive/riscos/xlogo.h`) |
| `evloop/` | The Wimp_Poll event loop (`rwl`), its test task and results (Phase 2, step 1) |
| `guest/` | RISC OS-side files for the machine's HostFS `XDev` folder: launchers (`RunX`, `RunXvfb`) and small BASIC helpers |
| `phase0/` | Toolchain smoke test |
| `sources/SHA256SUMS` | Checksums of the upstream tarballs (the tarballs themselves are not in git) |

## Reproduce

Needs Docker (the user in the `docker` group), git, wget, patch and the usual
archive tools on the host.

```
./setup.sh      # checks host tools, fetches and patches sources, builds the
                # Docker image, clones GCCSDK (for SharedULib) into gccsdk/
./build.sh      # deps, keymaps, xorg-server, SharedULib, AIF: all into stage/
./stage.sh      # copy the servers and guest/ to the emulator's HostFS
./dist.sh       # or: the !Xriscos application, and a zip of it, in dist/
```

`./build.sh xserver aif` reruns just those steps (`deps xkb xserver sul aif`).
The individual steps, which those scripts run:

```
./fetch-sources.sh
docker build -t xdev-gccsdk:1 docker
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/xdev xdev-gccsdk:1 /xdev/build-deps.sh
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/xdev xdev-gccsdk:1 /xdev/xkb/make-xkm.sh
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/xdev xdev-gccsdk:1 /xdev/build-xserver.sh
./build-sul.sh gccsdk                    # SharedULib,ffa into stage/
./stage.sh
tools/start-xvfb.sh                      # emulator must be running
DISPLAY=localhost:100 xdpyinfo           # NAT forward host 6100 -> guest 6000
```

`build/`, `prefix/`, `logs/`, `stage/`, `gccsdk/` and the tarballs are generated and not
in git. On the machine, HostFS `XDev` needs `Xvfb`, `SharedULib`, the
`xkb.precompiled` keymaps from `stage/xkb`, and the files in `guest/`.

## Distribution

`./dist.sh` (after `build.sh`) makes `dist/!Xriscos`, an ordinary RISC OS
application: double-click it to start the server, with the same arguments
as `XDev.RunX` (`*Run <Xriscos$Dir>.!Run -rootless` passes them on).

| In `!Xriscos` | From |
|---|---|
| `!Run`, `!Boot`, `!Help` | `app/` |
| `!Sprites`, `!Sprites22` | `tools/mklogo.py --app`: the icon bar's X logo, large and small, mode 12 and mode 20 |
| `!RunImage` | `stage/Xriscos,ff8` |
| `SharedULib` | `stage/SharedULib,ffa`, loaded by `!Run` unless a SharedUnixLibrary is |
| `xkb/precompiled/*.xkm` | `stage/xkb` |
| `Licence` | `LICENSE` and xorg-server's `COPYING` |

The files have HostFS `,xxx` filetype suffixes, so the folder can be copied
straight to a HostFS (`./dist.sh <dir>` does that, keeping an `Options`
already there). `dist/Xriscos-<git describe>.zip` holds the same, the
suffixes turned into Acorn extra fields (`ARC0`, ID 0x4341), as SparkFS and
Info-ZIP on RISC OS write and read them. Unzipped on Linux, the files lose
their types.
