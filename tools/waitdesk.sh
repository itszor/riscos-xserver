#!/bin/bash
# Wait (up to $1 s, default 180) until the RISC OS desktop's icon bar is drawn,
# judged from a VNC screenshot: the bottom rows stop being plain grey.
T=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp --suffix=.png); trap 'rm -f "$OUT"' EXIT
end=$((SECONDS + ${1:-180}))
while [ $SECONDS -lt $end ]; do
    if python3 -I "$T/vncshot.py" 5931 "$OUT" >/dev/null 2>&1 &&
       python3 -I - "$OUT" <<'PY'
import sys, zlib, struct
d = open(sys.argv[1], "rb").read()
w, h = struct.unpack(">II", d[16:24])
idat = b"".join(d[i + 8:i + 8 + struct.unpack(">I", d[i:i + 4])[0]]
                for i in range(8, len(d)) if d[i + 4:i + 8] == b"IDAT")
raw = zlib.decompress(idat)
row = raw[(h - 20) * (w * 3 + 1) + 1:(h - 19) * (w * 3 + 1)]
colours = {row[i:i + 3] for i in range(0, len(row), 3)}
sys.exit(0 if len(colours) > 8 else 1)
PY
    then
        echo "desktop up after $SECONDS s"; exit 0
    fi
    sleep 3
done
echo "desktop did not appear"; exit 1
