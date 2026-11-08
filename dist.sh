#!/bin/bash
# Put together !Xriscos, the RISC OS application, from build.sh's stage/ and
# the files in app/, and zip it for distribution:
#
#   dist/!Xriscos/             the application, HostFS-style (",xxx" filetype
#                              suffixes): copy it straight to a HostFS
#   dist/Xriscos-VERSION.zip   the same, with RISC OS filetypes in the zip
#                              (SparkFS, Info-ZIP unzip on RISC OS)
#
# Usage: dist.sh [HOSTFS-DIR]
#   HOSTFS-DIR   also copy !Xriscos there (replacing one already there, but
#                keeping its Options), e.g. ".../machines/My RiscPC/hostfs/Apps"
#
#   VERSION=<v>  the zip's version (default: git describe)
#
# Run build.sh first.
set -euo pipefail
cd "$(dirname "$0")"

case ${1:-} in -h|--help) sed -n '2,17s/^# \{0,1\}//p' "$0"; exit 0 ;; esac

for f in "stage/Xriscos,ff8" "stage/SharedULib,ffa" stage/xkb/precompiled; do
    [ -e "$f" ] || { echo "no $f: run ./build.sh first" >&2; exit 1; }
done
ls stage/xkb/precompiled/*.xkm >/dev/null 2>&1 ||
    { echo "no keymaps in stage/xkb/precompiled: run ./build.sh xkb" >&2; exit 1; }

VERSION=${VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo unknown)}
APP='dist/!Xriscos'

rm -rf "$APP"
mkdir -p "$APP/xkb/precompiled"

cp app/* "$APP/"
python3 tools/mklogo.py --app Xriscos "$APP/!Sprites,ff9" "$APP/!Sprites22,ff9"
cp "stage/Xriscos,ff8" "$APP/!RunImage,ff8"
cp "stage/SharedULib,ffa" "$APP/SharedULib,ffa"
for k in stage/xkb/precompiled/*.xkm; do
    cp "$k" "$APP/xkb/precompiled/$(basename "$k"),ffd"
done
{
    echo "Xriscos ($VERSION)"
    echo "==================="
    echo
    cat LICENSE
    echo
    echo
    echo "xorg-server 1.19.7, from which the server is built, is under these"
    echo "licences (its COPYING file):"
    echo
    cat build/xorg-server-1.19.7/COPYING
} > "$APP/Licence,fff"

zip="dist/Xriscos-$VERSION.zip"
rm -f dist/Xriscos-*.zip
python3 tools/riscos-zip.py "$zip" "$APP"

if [ $# -gt 0 ]; then
    dest="$1/!Xriscos"
    opts=
    [ -f "$dest/Options" ] && opts=$(mktemp) && cp "$dest/Options" "$opts"
    rm -rf "$dest"
    cp -r "$APP" "$dest"
    [ -n "$opts" ] && mv "$opts" "$dest/Options"
    echo "installed in $dest"
fi

find "$APP" -type f | sort | xargs ls -l
ls -l "$zip"
