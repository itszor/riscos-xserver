#!/usr/bin/env python3
"""Press and release one key over RFB (VNC, no password).

Usage: vnckey.py PORT KEYSYM [HOST]    e.g. vnckey.py 5931 0xff1b  (Escape)
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
    port, keysym = int(sys.argv[1]), int(sys.argv[2], 0)
    host = sys.argv[3] if len(sys.argv) > 3 else "127.0.0.1"
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
    for down in (1, 0):
        s.sendall(struct.pack(">BBxxI", 4, down, keysym))
        time.sleep(0.4)
    s.close()


if __name__ == "__main__":
    main()
