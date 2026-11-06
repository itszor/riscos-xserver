#!/bin/bash
# Build the SharedUnixLibrary module (SharedULib,ffa) that UnixLib programs
# need on RISC OS 3.70, from the GCCSDK commit matching the Docker image's
# UnixLib (625a8f03, Jan 2021; module version 1.16).
#
# Usage: build-sul.sh [path to a GCCSDK git clone]   (default ~/stuff/gccsdk/src)
set -euo pipefail
cd "$(dirname "$0")"
GCCSDK=${1:-$HOME/stuff/gccsdk/src}
OUT=$PWD/stage
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
git -C "$GCCSDK" archive 625a8f03 gcc4/recipe/files/gcc/libunixlib | tar -x -C "$tmp"
mkdir -p "$OUT"
docker run --rm -u "$(id -u):$(id -g)" -v "$tmp/gcc4/recipe/files/gcc/libunixlib":/ul:ro \
    -v "$OUT":/out xdev-gccsdk:1 bash -c '
    cd /ul &&
    arm-unknown-riscos-gcc -x assembler-with-cpp -I/ul/incl-local -I/ul/include \
        -c module/sul.s -o /tmp/sul.o &&
    arm-unknown-riscos-strip -O binary -o "/out/SharedULib,ffa" /tmp/sul.o'
ls -l "$OUT/SharedULib,ffa"
