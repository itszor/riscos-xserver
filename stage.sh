#!/bin/bash
# Rebuild the servers, convert them to AIF and copy them, with the RISC OS-side
# helpers in guest/, to the My RiscPC HostFS (run on the host).
set -euo pipefail
X=$(cd "$(dirname "$0")" && pwd)
H="${RPCEMU_DIR:-$HOME/stuff/rpcemu-extended}/machines/My RiscPC/hostfs/XDev"
docker run --rm -u "$(id -u):$(id -g)" -v "$X:/xdev" xdev-gccsdk:1 bash -c '
  /xdev/build-xserver.sh make >/dev/null &&
  cd /xdev/build/xorg-server-1.19.7 &&
  for s in hw/vfb/Xvfb hw/kdrive/riscos/Xriscos; do
    n=$(basename $s)
    arm-unknown-riscos-strip -o /xdev/logs/$n.stripped $s &&
    elf2aif /xdev/logs/$n.stripped "/xdev/logs/$n,ff8" >/dev/null || exit 1
  done'
cp "$X/logs/Xvfb,ff8" "$X/logs/Xriscos,ff8" "$H/"
cp "$X"/guest/* "$H/"
ls -l "$H/Xvfb,ff8" "$H/Xriscos,ff8"
