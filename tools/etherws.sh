#!/bin/bash
# Dump EtherRPCEm's receive-side workspace fields (workspace at $1, hex).
RPCEMU_DIR=${RPCEMU_DIR:-$HOME/stuff/rpcemu-extended}
D=("$RPCEMU_DIR/build/bin/rpcemu-debug" --socket "$RPCEMU_DIR/machines/My RiscPC/rpcemu-debug.sock")
"${D[@]}" mem "$(printf '%x' $((0x$1 + 56)))" 28 | python3 -c '
import json,sys,struct
d=bytes.fromhex(json.load(sys.stdin)["data"])
tx,unw,rx,_,sema,hdr,data=struct.unpack("<7I",d)
print(f"tx_frames={tx} rx_frames={rx} unwanted={unw} sema={sema} rxhdr_mbuf={hdr:#x} data_mbuf={data:#x}")'
