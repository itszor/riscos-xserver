#!/bin/bash
# Rebuild Xvfb, convert to AIF and copy to the My RiscPC HostFS (run on the host).
set -euo pipefail
X=$(cd "$(dirname "$0")" && pwd)
H="${RPCEMU_DIR:-$HOME/stuff/rpcemu-extended}/machines/My RiscPC/hostfs/XDev"
docker run --rm -u "$(id -u):$(id -g)" -v "$X:/xdev" xdev-gccsdk:1 bash -c '
  /xdev/build-xserver.sh make >/dev/null &&
  cd /xdev/build/xorg-server-1.19.7/hw/vfb &&
  arm-unknown-riscos-strip -o /xdev/logs/Xvfb.stripped Xvfb &&
  elf2aif /xdev/logs/Xvfb.stripped /xdev/logs/Xvfb,ff8 >/dev/null'
cp "$X/logs/Xvfb,ff8" "$H/"
ls -l "$H/Xvfb,ff8"
