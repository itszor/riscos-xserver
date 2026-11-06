#!/bin/bash
# Download the upstream sources, check them against sources/SHA256SUMS, unpack
# them into build/ and apply our patches. Runs on the host. Safe to re-run:
# trees already unpacked are left alone (delete build/<name> to redo one).
set -euo pipefail
cd "$(dirname "$0")"

X=https://www.x.org/releases/individual
urls=(
    $X/xserver/xorg-server-1.19.7.tar.bz2
    $X/proto/xorgproto-2019.2.tar.bz2
    $X/lib/xtrans-1.4.0.tar.bz2
    $X/lib/libXau-1.0.9.tar.bz2
    $X/lib/libfontenc-1.1.4.tar.bz2
    $X/lib/libXfont2-2.0.4.tar.bz2
    $X/lib/libxkbfile-1.1.0.tar.bz2
    https://www.cairographics.org/releases/pixman-0.38.4.tar.gz
    https://zlib.net/fossils/zlib-1.2.13.tar.gz
)

mkdir -p sources build
for u in "${urls[@]}"; do
    f=sources/$(basename "$u")
    [ -s "$f" ] || { echo "==> fetching $u"; wget -q -O "$f" "$u"; }
done
(cd sources && sha256sum -c --quiet SHA256SUMS)

for f in sources/*.tar.*; do
    name=$(basename "$f"); name=${name%.tar.*}
    [ -d "build/$name" ] && continue
    echo "==> unpacking $name"
    tar xf "$f" -C build
    if [ -f "patches/$name-riscos.diff" ]; then
        echo "==> patching $name"
        patch -d "build/$name" -p1 < "patches/$name-riscos.diff"
    fi
done
