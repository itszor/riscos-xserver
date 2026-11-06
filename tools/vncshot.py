#!/usr/bin/env python3
"""Grab one frame from an RFB (VNC) server with no password and save it as PNG.

Usage: vncshot.py PORT OUT.png [HOST]
"""
import socket
import struct
import sys
import zlib


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError("VNC server closed the connection")
        buf += chunk
    return buf


def write_png(path, w, h, rgb_rows):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rgb_rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    port = int(sys.argv[1])
    out = sys.argv[2]
    host = sys.argv[3] if len(sys.argv) > 3 else "127.0.0.1"
    s = socket.create_connection((host, port), timeout=20)
    recv_exact(s, 12)
    s.sendall(b"RFB 003.008\n")
    ntypes = recv_exact(s, 1)[0]
    types = recv_exact(s, ntypes)
    if 1 not in types:
        sys.exit("server requires a password; not supported")
    s.sendall(b"\x01")
    if struct.unpack(">I", recv_exact(s, 4))[0] != 0:
        sys.exit("VNC security handshake failed")
    s.sendall(b"\x01")  # shared
    w, h = struct.unpack(">HH", recv_exact(s, 4))
    recv_exact(s, 16)
    name_len = struct.unpack(">I", recv_exact(s, 4))[0]
    recv_exact(s, name_len)
    # 32bpp, depth 24, little endian, true colour, R at bit 16, G 8, B 0
    pf = struct.pack(">BBBBHHHBBB3x", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
    s.sendall(b"\x00\x00\x00\x00" + pf)
    s.sendall(struct.pack(">BBHi", 2, 0, 1, 0))  # raw encoding only
    s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, w, h))
    fb = bytearray(w * h * 3)
    while True:
        msg = recv_exact(s, 1)[0]
        if msg == 0:
            break
        if msg == 2:  # bell
            continue
        if msg == 3:  # cut text
            recv_exact(s, 3)
            recv_exact(s, struct.unpack(">I", recv_exact(s, 4))[0])
            continue
        if msg == 1:  # colour map entries
            _, _, n = struct.unpack(">BHH", recv_exact(s, 5))
            recv_exact(s, n * 6)
            continue
        sys.exit("unexpected VNC message %d" % msg)
    recv_exact(s, 1)
    nrects = struct.unpack(">H", recv_exact(s, 2))[0]
    for _ in range(nrects):
        x, y, rw, rh, enc = struct.unpack(">HHHHi", recv_exact(s, 12))
        if enc == -223:  # DesktopSize pseudo-encoding
            continue
        if enc != 0:
            sys.exit("unexpected encoding %d" % enc)
        data = recv_exact(s, rw * rh * 4)
        for row in range(rh):
            src = data[row * rw * 4:(row + 1) * rw * 4]
            dst = ((y + row) * w + x) * 3
            for col in range(rw):
                b, g, r = src[col * 4], src[col * 4 + 1], src[col * 4 + 2]
                fb[dst + col * 3:dst + col * 3 + 3] = bytes((r, g, b))
    s.close()
    rows = [bytes(fb[r * w * 3:(r + 1) * w * 3]) for r in range(h)]
    write_png(out, w, h, rows)
    print("%s: %dx%d" % (out, w, h))


if __name__ == "__main__":
    main()
