#!/usr/bin/env python3
"""Click at a screen position over RFB (VNC, no password).

Usage: vncclick.py PORT X Y [select|menu|adjust] [HOST]
"""
import socket
import struct
import sys
import time

BUTTONS = {"select": 1, "menu": 2, "adjust": 4}


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError("VNC server closed the connection")
        buf += chunk
    return buf


def main():
    port, x, y = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    mask = BUTTONS[sys.argv[4] if len(sys.argv) > 4 else "select"]
    host = sys.argv[5] if len(sys.argv) > 5 else "127.0.0.1"
    s = socket.create_connection((host, port), timeout=20)
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
    s.sendall(struct.pack(">BBHH", 5, 0, x, y))
    time.sleep(0.4)
    s.sendall(struct.pack(">BBHH", 5, mask, x, y))
    time.sleep(0.4)
    s.sendall(struct.pack(">BBHH", 5, 0, x, y))
    time.sleep(0.5)
    s.close()


if __name__ == "__main__":
    main()
