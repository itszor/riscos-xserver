#!/bin/bash
# Count the SWIs called from application space (the foreground task) over
# $1 seconds (default 5), grouped by SWI and caller. Uses the emulator's SWI
# trace; the ring holds 128 events, so it is read every 100 ms.
RPCEMU_DIR=${RPCEMU_DIR:-$HOME/stuff/rpcemu-extended}
D=("$RPCEMU_DIR/build/bin/rpcemu-debug" --socket "$RPCEMU_DIR/machines/My RiscPC/rpcemu-debug.sock")
"${D[@]}" trace config swi_log=1 swi_min=0 swi_max=0xffffffff >/dev/null
"${D[@]}" trace 128 >/dev/null
end=$(( $(date +%s%N) / 1000000 + ${1:-5} * 1000 ))
while [ $(( $(date +%s%N) / 1000000 )) -lt $end ]; do
    "${D[@]}" trace 128
    sleep 0.1
done | python3 -c '
import json, sys, collections
c = collections.Counter(); dropped = 0
for line in sys.stdin:
    d = json.loads(line); dropped += d.get("dropped", 0)
    for e in d.get("events", []):
        pc = int(e["pc"], 16) & 0x03fffffc
        if 0x8000 <= pc < 0x1c00000:
            c[(e.get("arg0"), hex(pc))] += 1
print("app-space SWIs:", sum(c.values()), "(ring overflows: %d)" % dropped)
for k, v in c.most_common(8): print("  ", v, "SWI &%s from %s" % k)'
"${D[@]}" trace config swi_log=0 >/dev/null
