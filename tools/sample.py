#!/usr/bin/env python3
"""Poor man's profiler for the emulated ARM: pause, record PC/backtrace, resume.

Usage: sample.py SOCKET SYMFILE [N]   (SOCKET: the machine's rpcemu-debug.sock)
"""
import collections
import json
import os
import subprocess
import sys
import time

DEBUG = os.path.join(os.environ.get("RPCEMU_DIR", os.path.expanduser("~/stuff/rpcemu-extended")),
                     "build", "bin", "rpcemu-debug")


def cmd(sock, *args):
    out = subprocess.run([DEBUG, "--socket", sock, *args], capture_output=True,
                         text=True, timeout=20)
    try:
        return json.loads(out.stdout)
    except ValueError:
        return {"ok": False, "error": (out.stdout + out.stderr).strip()}


def main():
    sock, symfile = sys.argv[1], sys.argv[2]
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    print("sym load:", cmd(sock, "sym", "load", symfile).get("count"))
    seen = collections.Counter()
    for _ in range(n):
        cmd(sock, "pause")
        for _ in range(50):
            st = cmd(sock, "status")
            if st.get("paused"):
                break
            time.sleep(0.05)
        regs = cmd(sock, "regs")
        bt = cmd(sock, "bt", "10")
        frames = [f"{f.get('pc')} {f.get('symbol')}+{f.get('offset')}"
                  for f in bt.get("frames", [])]
        print(f"pc={regs.get('pc')} mode={regs.get('mode')}")
        for f in frames:
            print("    ", f)
        if frames:
            seen[frames[0].split(" ", 1)[1]] += 1
        cmd(sock, "resume")
        time.sleep(0.7)
    print("top frames:", seen.most_common(5))


if __name__ == "__main__":
    main()
