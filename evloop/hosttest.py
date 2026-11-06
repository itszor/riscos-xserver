#!/usr/bin/env python3
"""Exercise evtest (the Wimp_Poll loop prototype) from the host.

Usage: hosttest.py [host] [port]     default localhost 6100 (NAT forward to 6000)
"""
import socket
import statistics
import sys
import threading
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "localhost"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6100


def connect():
    s = socket.create_connection((HOST, PORT), timeout=30)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed")
        buf += chunk
    return buf


def recv_line(s):
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(1)
        if not chunk:
            raise EOFError("connection closed")
        buf += chunk
    return buf.decode().strip()


def stats(s):
    s.sendall(b"stats\n")
    line = recv_line(s)
    return dict(kv.split("=") for kv in line.split())


def main():
    s = connect()
    st0 = stats(s)
    print("connected; stats:", st0)

    # 1. Idle: how often does the loop wake when nothing happens?
    time.sleep(10)
    st1 = stats(s)
    polls = int(st1["polls"]) - int(st0["polls"])
    nulls = int(st1["null"]) - int(st0["null"])
    ticks = int(st1["ticks"]) - int(st0["ticks"])
    print(f"idle 10s: {polls} Wimp polls, {nulls} null events, {ticks} timer ticks, "
          f"max tick lateness {st1['max_tick_late_ms']} ms")

    # 2. Latency: small request/response round trips.
    rtts = []
    for i in range(200):
        msg = (b"%031d\n" % i)
        t0 = time.perf_counter()
        s.sendall(msg)
        recv_exact(s, len(msg))
        rtts.append((time.perf_counter() - t0) * 1000)
    rtts.sort()
    print(f"round trip, 32 bytes x200: median {statistics.median(rtts):.2f} ms, "
          f"p95 {rtts[int(len(rtts) * 0.95)]:.2f} ms, max {rtts[-1]:.2f} ms")

    # 3. Throughput: echo 1 MB, reading while writing.
    total = 1 << 20
    payload = bytes((i * 7) & 0xFF for i in range(65536))
    received = bytearray()

    def reader():
        while len(received) < total:
            chunk = s.recv(65536)
            if not chunk:
                break
            received.extend(chunk)

    t = threading.Thread(target=reader)
    t0 = time.perf_counter()
    t.start()
    for _ in range(total // len(payload)):
        s.sendall(payload)
    t.join(timeout=120)
    dt = time.perf_counter() - t0
    ok = bytes(received) == payload * (total // len(payload))
    print(f"echo 1 MB: {dt:.2f} s, {total / 1024 / dt:.0f} KB/s each way, "
          f"data {'intact' if ok else 'CORRUPT (%d bytes)' % len(received)}")

    # 4. A second client at the same time.
    s2 = connect()
    s2.sendall(b"second client\n")
    s.sendall(b"first client\n")
    a = recv_exact(s, 13)
    b = recv_exact(s2, 14)
    print("two clients:", "ok" if (a, b) == (b"first client\n", b"second client\n") else (a, b))
    s2.close()

    st2 = stats(s)
    print("final stats:", st2)
    s.sendall(b"quit\n")
    s.close()


if __name__ == "__main__":
    main()
