#!/bin/bash
# Build the X servers for RISC OS (run setup.sh first). Runs the build steps in
# the xdev-gccsdk:1 image and leaves everything the machine needs in stage/:
#
#   stage/Xriscos,ff8         the desktop X server (AIF)
#   stage/Xvfb,ff8            the framebuffer-only server (AIF)
#   stage/SharedULib,ffa      SharedUnixLibrary module for RISC OS 3.70
#   stage/xkb/precompiled/    precompiled XKB keymaps
#
# Usage: build.sh [step...]
#   steps: deps xkb xserver sul aif   (default: all, in that order)
#
#   GCCSDK=<dir>   the GCCSDK git clone made by setup.sh (default: ./gccsdk)
#
# stage.sh then copies the servers and guest/ to the emulator's HostFS.
set -euo pipefail
cd "$(dirname "$0")"
X=$PWD

IMAGE=xdev-gccsdk:1
GCCSDK=${GCCSDK:-$PWD/gccsdk}
SERVER=build/xorg-server-1.19.7

steps=("$@")
[ ${#steps[@]} -gt 0 ] || steps=(deps xkb xserver sul aif)
for s in "${steps[@]}"; do
    case $s in
        deps|xkb|xserver|sul|aif) ;;
        -h|--help) sed -n '2,16s/^# \{0,1\}//p' "$0"; exit 0 ;;
        *) echo "unknown step: $s (deps xkb xserver sul aif)" >&2; exit 2 ;;
    esac
done

step() { echo "==> $*"; }
in_image() { docker run --rm -u "$(id -u):$(id -g)" -v "$X:/xdev" "$IMAGE" "$@"; }

# Fail early, with a pointer to setup.sh, if it hasn't been run.
docker image inspect "$IMAGE" >/dev/null 2>&1 ||
    { echo "no Docker image $IMAGE: run ./setup.sh first" >&2; exit 1; }
[ -d "$SERVER" ] ||
    { echo "no $SERVER: run ./setup.sh first" >&2; exit 1; }
mkdir -p stage logs

for s in "${steps[@]}"; do
    case $s in
    deps)
        step "dependencies (logs in logs/)"
        in_image /xdev/build-deps.sh ;;
    xkb)
        step "XKB keymaps"
        in_image /xdev/xkb/make-xkm.sh ;;
    xserver)
        step "xorg-server (Xvfb, Xriscos)"
        in_image /xdev/build-xserver.sh ;;
    sul)
        step "SharedUnixLibrary"
        [ -d "$GCCSDK/.git" ] ||
            { echo "no GCCSDK clone in $GCCSDK: run ./setup.sh first" >&2; exit 1; }
        ./build-sul.sh "$GCCSDK" ;;
    aif)
        step "converting to AIF"
        in_image bash -c '
            set -e
            cd /xdev/build/xorg-server-1.19.7
            for s in hw/vfb/Xvfb hw/kdrive/riscos/Xriscos; do
                n=$(basename $s)
                arm-unknown-riscos-strip -o /xdev/logs/$n.stripped $s
                elf2aif /xdev/logs/$n.stripped "/xdev/stage/$n,ff8" >/dev/null
            done' ;;
    esac
done

step "done"
ls -l stage/
