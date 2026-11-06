#!/bin/bash
# Bring up Xvfb on the "My RiscPC" emulated machine and wait until it answers.
RPCEMU_DIR=${RPCEMU_DIR:-$HOME/stuff/rpcemu-extended}
RUN=("$RPCEMU_DIR/build/bin/rpcemu-run" --machine "My RiscPC" --socket "$RPCEMU_DIR/machines/My RiscPC/hostcmd.sock" --)
LOG="$RPCEMU_DIR/machines/My RiscPC/hostfs/XDev/xvfblog"
for i in $(seq 1 90); do
    timeout 15 "${RUN[@]}" 'Echo up' 2>/dev/null | grep -q '^up' && break
    sleep 2
done
# HostCmd answers before !Boot has configured the network; Xvfb cannot bind
# its listener until then. Inet$Started is set at the end of network setup.
for i in $(seq 1 60); do
    timeout 15 "${RUN[@]}" 'Show Inet$Started' 2>/dev/null | grep -q 'Yes' && break
    sleep 2
done
rm -f "$LOG"
timeout 30 "${RUN[@]}" 'Obey HostFS::HostFS.$.XDev.RunXvfb' >/dev/null
timeout 15 "${RUN[@]}" 'Dir HostFS::HostFS.$.XDev' >/dev/null
for i in $(seq 1 30); do
    if timeout 20 "${RUN[@]}" 'xping' 2>/dev/null | grep 'xping:'; then exit 0; fi
    sleep 2
done
echo "Xvfb did not come up"; cat "$LOG" 2>/dev/null; exit 1
