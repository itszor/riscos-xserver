#!/usr/bin/env python3
"""Zip a directory of HostFS-style files for RISC OS.

Files on the host carry their RISC OS filetype as a ",xxx" suffix, as
RPCEmu's HostFS and NFS do (`!Run,feb`, `!RunImage,ff8`). In the zip the
suffix is dropped and the type goes in the Acorn extra field (ID 0x4341,
"ARC0": load and exec address, attributes), as SparkFS and Info-ZIP's
RISC OS port write and read it, so unzipping on RISC OS gives the files
their types. Files with no suffix are typed as text (fff).

Names are kept as on the host, "/" separating directories: a host
"us.xkm" is RISC OS "us/xkm" (the same as HostFS shows it).

Usage: riscos-zip.py OUT.zip DIR [DIR...]
  Each DIR is stored under its own name (its last path component).
"""
import os
import re
import struct
import sys
import time
import zipfile

SUFFIX = re.compile(r",([0-9a-fA-F]{3})$")
ACORN_ID = 0x4341
ATTR = 0x13             # owner read/write, public read
RISCOS_EPOCH = -2208988800   # 1900-01-01 in Unix time


def acorn_extra(filetype, mtime):
    cs = int((mtime - RISCOS_EPOCH) * 100) & 0xFFFFFFFFFF
    load = 0xFFF00000 | (filetype << 8) | (cs >> 32)
    exec_ = cs & 0xFFFFFFFF
    return struct.pack("<HH4sIIII", ACORN_ID, 20, b"ARC0", load, exec_,
                       ATTR, 0)


def add_tree(zf, top):
    base = os.path.dirname(os.path.abspath(top))
    for root, dirs, files in os.walk(top):
        dirs.sort()
        rel = os.path.relpath(root, base)
        st = os.stat(root)
        di = zipfile.ZipInfo(rel + "/", time.localtime(st.st_mtime)[:6])
        di.external_attr = (0o40755 << 16) | 0x10
        zf.writestr(di, b"")
        for name in sorted(files):
            path = os.path.join(root, name)
            m = SUFFIX.search(name)
            filetype = int(m.group(1), 16) if m else 0xFFF
            stored = SUFFIX.sub("", name)
            st = os.stat(path)
            zi = zipfile.ZipInfo(rel + "/" + stored,
                                 time.localtime(st.st_mtime)[:6])
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100644 | (st.st_mode & 0o111)) << 16
            zi.extra = acorn_extra(filetype, st.st_mtime)
            with open(path, "rb") as f:
                zf.writestr(zi, f.read())


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__.strip())
    out = sys.argv[1]
    with zipfile.ZipFile(out + ".tmp", "w") as zf:
        for top in sys.argv[2:]:
            add_tree(zf, top.rstrip("/"))
    os.replace(out + ".tmp", out)


if __name__ == "__main__":
    main()
