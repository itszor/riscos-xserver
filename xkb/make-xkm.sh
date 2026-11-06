#!/bin/bash
# Precompile XKB keymaps on the build host for the RISC OS X server, which
# cannot run xkbcomp. Output: $1/precompiled/<layout>.xkm (default = us).
# Keycodes are evdev (Linux scancode + 8); the RISC OS input driver maps its
# low-level key numbers to these.
set -euo pipefail
out=${1:-/xdev/stage/xkb}/precompiled
mkdir -p "$out"
for layout in us gb; do
    cat > /tmp/$layout.xkb <<KEYMAP
xkb_keymap {
    xkb_keycodes  { include "evdev+aliases(qwerty)" };
    xkb_types     { include "complete" };
    xkb_compat    { include "complete" };
    xkb_symbols   { include "pc+$layout+inet(evdev)" };
    xkb_geometry  { include "pc(pc105)" };
};
KEYMAP
    xkbcomp -w 1 -I/usr/share/X11/xkb -xkm /tmp/$layout.xkb "$out/$layout.xkm"
done
cp "$out/us.xkm" "$out/default.xkm"
ls -l "$out"
