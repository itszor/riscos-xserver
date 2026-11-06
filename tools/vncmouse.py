#!/usr/bin/env python3
"""Move the pointer, and optionally press/release buttons, over RFB (VNC).

Usage: vncmouse.py PORT STEP...    each STEP is one of
    X,Y            move there with no buttons
    X,Y,MASK       move there with these buttons down (1 left/Select,
                   2 middle/Menu, 4 right/Adjust)
    sleep:SECONDS  pause
"""
import socket
import struct
import sys
import time


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
    for step in sys.argv[2:]:
        if step.startswith("sleep:"):
            time.sleep(float(step[6:]))
            continue
        parts = [int(p) for p in step.split(",")]
        x, y = parts[0], parts[1]
        mask = parts[2] if len(parts) > 2 else 0
        s.sendall(struct.pack(">BBHH", 5, mask, x, y))
        time.sleep(0.15)
    time.sleep(0.3)
    s.close()


if __name__ == "__main__":
    main()
