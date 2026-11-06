#!/bin/bash
# Cross-build the X server's dependencies for RISC OS (arm-unknown-riscos),
# as static libraries installed into $PREFIX. Runs inside the xdev-gccsdk image:
#
#   docker run --rm -u $(id -u):$(id -g) -v "$PWD":/xdev \
#       xdev-gccsdk:1 /xdev/build-deps.sh
set -euo pipefail

XDEV=/xdev
SRC=$XDEV/build
PREFIX=$XDEV/prefix
LOGS=$XDEV/logs
mkdir -p "$PREFIX" "$LOGS"

export PATH=/home/riscos/cross/bin:$PATH
export HOST=arm-unknown-riscos
export CC=$HOST-gcc AR=$HOST-ar RANLIB=$HOST-ranlib STRIP=$HOST-strip
# ARMv3 code generation (GCCSDK's default for this target) is deliberate: the
# Risc PC bus does not support halfword transfers, so no -march=armv4.
export CFLAGS="-O2 -mtune=strongarm"
export CPPFLAGS="-I$PREFIX/include"
export LDFLAGS="-L$PREFIX/lib -static"
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig
export PKG_CONFIG_PATH=
export ACLOCAL_PATH=$PREFIX/share/aclocal:/usr/share/aclocal

step() { echo "==> $*"; }

configure_make() {   # dir, extra configure args...
    local dir=$1; shift
    local stamp=$PREFIX/.built-$(basename "$dir")
    [ -e "$stamp" ] && { step "$dir already built"; return; }
    step "building $dir"
    cd "$SRC/$dir"
    ./configure --host=$HOST --prefix="$PREFIX" --disable-shared --enable-static "$@" \
        > "$LOGS/$dir.configure.log" 2>&1 || { tail -30 "$LOGS/$dir.configure.log"; exit 1; }
    make -j"$(nproc)" ${MAKEARGS:-} > "$LOGS/$dir.make.log" 2>&1 || { tail -40 "$LOGS/$dir.make.log"; exit 1; }
    make install ${MAKEARGS:-} > "$LOGS/$dir.install.log" 2>&1 || { tail -30 "$LOGS/$dir.install.log"; exit 1; }
    touch "$stamp"
}

# zlib has its own configure script, not autoconf.
if [ ! -e "$PREFIX/.built-zlib" ]; then
    step "building zlib"
    cd "$SRC/zlib-1.2.13"
    CHOST=$HOST ./configure --static --prefix="$PREFIX" > "$LOGS/zlib.configure.log" 2>&1
    make -j"$(nproc)" libz.a > "$LOGS/zlib.make.log" 2>&1
    make install > "$LOGS/zlib.install.log" 2>&1
    touch "$PREFIX/.built-zlib"
fi

# libsha1: our own minimal implementation (libsha1/).
if [ ! -e "$PREFIX/.built-libsha1" ]; then
    step "building libsha1"
    mkdir -p "$PREFIX/lib/pkgconfig" "$PREFIX/include"
    cd "$XDEV/libsha1"
    $CC $CFLAGS -c sha1.c -o "$LOGS/sha1.o"
    rm -f "$PREFIX/lib/libsha1.a"
    $AR rcs "$PREFIX/lib/libsha1.a" "$LOGS/sha1.o"
    cp libsha1.h "$PREFIX/include/"
    cat > "$PREFIX/lib/pkgconfig/libsha1.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: libsha1
Description: Minimal SHA-1 for the RISC OS X server port
Version: 0.3
Libs: -L\${libdir} -lsha1
Cflags: -I\${includedir}
EOF
    touch "$PREFIX/.built-libsha1"
fi

configure_make xorgproto-2019.2
configure_make xtrans-1.4.0
configure_make libXau-1.0.9
configure_make libfontenc-1.1.4
# RISC OS has no symlinks, so O_NOFOLLOW (missing from UnixLib) can be 0.
# Its test utilities trip a UnixLib assert.h quirk, so skip them (MAKEARGS).
MAKEARGS="noinst_PROGRAMS=" CPPFLAGS="$CPPFLAGS -DO_NOFOLLOW=0" configure_make libXfont2-2.0.4 --disable-freetype --disable-devel-docs \
    --disable-ipv6 --disable-unix-transport --disable-local-transport
# pixman: plain C. Its ARM fast paths need ARMv6 SIMD or NEON, which
# StrongARM does not have.
configure_make pixman-0.38.4 --disable-arm-simd --disable-arm-neon \
    --disable-arm-iwmmxt --disable-gtk --disable-libpng --disable-openmp \
    --disable-timers

# xorg-server 1.19 includes two headers from libxkbfile and lists the xkbfile
# pkg-config module, but uses its own xkb/xkbfile.h and links nothing from the
# library. Copying the headers and a stub .pc avoids porting libX11 for it.
mkdir -p "$PREFIX/include/X11/extensions" "$PREFIX/lib/pkgconfig"
cp "$SRC/libxkbfile-1.1.0/include/X11/extensions/XKM.h" \
   "$SRC/libxkbfile-1.1.0/include/X11/extensions/XKMformat.h" \
   "$PREFIX/include/X11/extensions/"
cat > "$PREFIX/lib/pkgconfig/xkbfile.pc" <<PC
# Stub: see build-deps.sh.
Name: xkbfile
Description: stub for the RISC OS X server port
Version: 1.1.0
Libs:
Cflags:
PC

step "dependencies done"
ls "$PREFIX/lib"
