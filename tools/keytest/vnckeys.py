#!/usr/bin/env python3
"""Press and release keys over RFB (VNC), by X keysym name.

Usage: vnckeys.py PORT NAME...   e.g. vnckeys.py 5931 a b Shift_L Return
       vnckeys.py PORT --type TEXT  type TEXT (US layout), then Return
A NAME of the form +Shift_L holds a key down, -Shift_L lets it go.
"""
import socket
import struct
import sys
import time

KEYSYMS = {
    "BackSpace": 0xff08, "Tab": 0xff09, "Return": 0xff0d, "Escape": 0xff1b,
    "Delete": 0xffff, "Home": 0xff50, "Left": 0xff51, "Up": 0xff52,
    "Right": 0xff53, "Down": 0xff54, "Page_Up": 0xff55, "Page_Down": 0xff56,
    "End": 0xff57, "Insert": 0xff63, "Num_Lock": 0xff7f, "Scroll_Lock": 0xff14,
    "KP_Enter": 0xff8d, "KP_Home": 0xff95, "KP_Left": 0xff96, "KP_Up": 0xff97,
    "KP_Right": 0xff98, "KP_Down": 0xff99, "KP_Page_Up": 0xff9a,
    "KP_Page_Down": 0xff9b, "KP_End": 0xff9c, "KP_Insert": 0xff9e,
    "KP_Delete": 0xff9f, "KP_Multiply": 0xffaa, "KP_Add": 0xffab,
    "KP_Subtract": 0xffad, "KP_Decimal": 0xffae, "KP_Divide": 0xffaf,
    "Shift_L": 0xffe1, "Shift_R": 0xffe2, "Control_L": 0xffe3,
    "Control_R": 0xffe4, "Caps_Lock": 0xffe5, "Alt_L": 0xffe9, "Alt_R": 0xffea,
    "space": 0x20, "apostrophe": 0x27, "comma": 0x2c, "minus": 0x2d,
    "period": 0x2e, "slash": 0x2f, "semicolon": 0x3b, "equal": 0x3d,
    "bracketleft": 0x5b, "backslash": 0x5c, "bracketright": 0x5d,
    "grave": 0x60, "bar": 0x7c, "numbersign": 0x23, "sterling": 0xa3,
}
for i in range(10):
    KEYSYMS[str(i)] = 0x30 + i
    KEYSYMS["KP_%d" % i] = 0xffb0 + i
for c in "abcdefghijklmnopqrstuvwxyz":
    KEYSYMS[c] = ord(c)
for i in range(1, 13):
    KEYSYMS["F%d" % i] = 0xffbd + i


# US layout: shifted characters and the key that types them.
SHIFTED = dict(zip('~!@#$%^&*()_+{}|:"<>?', "`1234567890-=[]\\;',./"))
CHARS = {" ": "space", "-": "minus", "=": "equal", "[": "bracketleft",
         "]": "bracketright", "\\": "backslash", ";": "semicolon",
         "'": "apostrophe", "`": "grave", ",": "comma", ".": "period",
         "/": "slash"}


def type_keys(text):
    """Key steps that type text on a US layout."""
    steps = []
    for ch in text:
        shift = ch.isupper() or ch in SHIFTED
        base = SHIFTED.get(ch, ch.lower())
        name = CHARS.get(base, base)
        steps += ["+Shift_L", name, "-Shift_L"] if shift else [name]
    return steps + ["Return"]


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError("VNC server closed the connection")
        buf += chunk
    return buf


def main():
    port = int(sys.argv[1])
    s = socket.create_connection(("127.0.0.1", port), timeout=20)
    recv_exact(s, 12)
    s.sendall(b"RFB 003.008\n")
    types = recv_exact(s, recv_exact(s, 1)[0])
    if 1 not in types:
        sys.exit("server requires a password; not supported")
    s.sendall(b"\x01")
    if struct.unpack(">I", recv_exact(s, 4))[0] != 0:
        sys.exit("VNC security handshake failed")
    s.sendall(b"\x01")
    recv_exact(s, 20)
    recv_exact(s, struct.unpack(">I", recv_exact(s, 4))[0])
    steps = type_keys(sys.argv[3]) if sys.argv[2] == "--type" else sys.argv[2:]
    for name in steps:
        if name[0] in "+-" and len(name) > 1:
            s.sendall(struct.pack(">BBxxI", 4, name[0] == "+", KEYSYMS[name[1:]]))
            time.sleep(0.15)
            continue
        for down in (1, 0):
            s.sendall(struct.pack(">BBxxI", 4, down, KEYSYMS[name]))
            time.sleep(0.12)
    time.sleep(0.3)
    s.close()


if __name__ == "__main__":
    main()
