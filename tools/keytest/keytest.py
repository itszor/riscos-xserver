#!/usr/bin/env python3
"""Check Xriscos's keyboard mapping end to end.

Sends one key per physical key over VNC to the emulator, while xev (from the
host) has the focus in the X window, and checks each KeyPress/KeyRelease xev
reports carries the X keycode the emulator's own VNC table gives that key
(vnc_keycodes.json, from rpcemu-extended's keysymToScanCode: X11 keycode =
evdev + 8, by physical position). xev's log is read from XEV_LOG.

Usage: keytest.py VNC_PORT XEV_LOG
"""
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
table = json.load(open(os.path.join(HERE, "vnc_keycodes.json")))
port, log = sys.argv[1], sys.argv[2]

# One keysym per physical key, in an order that leaves no modifier held.
keys = []
seen = set()
for name in (list("abcdefghijklmnopqrstuvwxyz") + list("0123456789") +
             ["minus", "equal", "bracketleft", "bracketright", "backslash",
              "semicolon", "apostrophe", "grave", "comma", "period", "slash",
              "sterling", "space", "Return", "Tab", "BackSpace", "Escape"] +
             ["F%d" % i for i in range(1, 13)] +
             ["Insert", "Delete", "Home", "End", "Page_Up", "Page_Down",
              "Up", "Down", "Left", "Right",
              "Shift_L", "Shift_R", "Control_L", "Control_R", "Alt_L", "Alt_R",
              "Scroll_Lock",
              "KP_Divide", "KP_Multiply", "KP_Subtract", "KP_Add", "KP_Enter",
              "KP_Decimal", "KP_0", "KP_1", "KP_2", "KP_3", "KP_4", "KP_5",
              "KP_6", "KP_7", "KP_8", "KP_9"]):
    code = table.get(name)
    if code is None or code in seen:
        continue
    seen.add(code)
    keys.append(name)

# Guard: one harmless key first. If xev did not see it, the keys would go to
# the RISC OS desktop instead (F12 opens the command line), so stop.
before = open(log).read().count("KeyPress event")
subprocess.run([sys.executable, "-I", os.path.join(HERE, "vnckeys.py"), port, "Shift_L"], check=True)
time.sleep(1.5)
if open(log).read().count("KeyPress event") == before:
    sys.exit("xev saw no key: the X window does not have the focus; not sending the rest")
before += 1
for i in range(0, len(keys), 12):
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "vnckeys.py"), port] + keys[i:i + 12],
                   check=True)
time.sleep(2)

events = re.findall(r"^(KeyPress|KeyRelease) event.*?\n.*?\n.*?keycode (\d+) \(keysym 0x[0-9a-f]+, (\w+)\)",
                    open(log).read(), re.M)
presses = [(int(c), n) for t, c, n in events if t == "KeyPress"][before:]
releases = [int(c) for t, c, n in events if t == "KeyRelease"]

bad = []
for i, name in enumerate(keys):
    want = table[name]
    got = presses[i] if i < len(presses) else (None, None)
    if got[0] != want:
        bad.append((name, want, got))
print(f"{len(keys)} keys sent, {len(presses)} KeyPress seen, "
      f"{len([r for r in releases])} KeyRelease seen in total")
for name, want, got in bad:
    print(f"  MISMATCH {name}: expected keycode {want}, xev got {got[0]} ({got[1]})")
print("all keys mapped correctly" if not bad else f"{len(bad)} mismatches")
sys.exit(1 if bad else 0)
