#!/bin/bash
# Cross-build xorg-server 1.19.7 for RISC OS. Phase 1 builds Xvfb only.
#
#   docker run --rm -u $(id -u):$(id -g) -v "$PWD":/xdev \
#       xdev-gccsdk:1 /xdev/build-xserver.sh [configure|make]
set -euo pipefail

XDEV=/xdev
SRC=$XDEV/build/xorg-server-1.19.7
PREFIX=$XDEV/prefix
LOGS=$XDEV/logs
mkdir -p "$LOGS"

export PATH=/home/riscos/cross/bin:$PATH
export HOST=arm-unknown-riscos
export CC=$HOST-gcc AR=$HOST-ar RANLIB=$HOST-ranlib STRIP=$HOST-strip
export CFLAGS="-O2 -mtune=strongarm"
export CPPFLAGS="-I$PREFIX/include"
# Force-included at make time only: during configure it would break the
# AC_CHECK_FUNCS probes (e.g. sigaction reported missing).
COMPAT_CPPFLAGS="$CPPFLAGS -include $XDEV/compat/riscos-compat.h"
export LDFLAGS="-L$PREFIX/lib -static"
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig
export PKG_CONFIG_PATH=

what=${1:-all}

# rwl, the Wimp_Poll event loop (evloop/), as a library the server links:
# os/ospoll.c calls it on RISC OS. Rebuilt every time; it is two small files.
echo "==> rwl"
$CC $CFLAGS -c "$XDEV/evloop/rwl.c" -o "$LOGS/rwl.o"
$CC -c "$XDEV/evloop/rwl_event.s" -o "$LOGS/rwl_event.o"
rm -f "$PREFIX/lib/librwl.a"
$AR rcs "$PREFIX/lib/librwl.a" "$LOGS/rwl.o" "$LOGS/rwl_event.o"
cp "$XDEV/evloop/rwl.h" "$PREFIX/include/"

cd "$SRC"

# Our patches change configure.ac and Makefile.am files (the Xriscos server),
# so regenerate the build system whenever one is newer than configure.
if [ -n "$(find . -name configure.ac -newer configure -o -name Makefile.am -newer configure | head -1)" ]; then
    echo "==> autoreconf"
    ACLOCAL_PATH=$PREFIX/share/aclocal:/usr/share/aclocal autoreconf -fi \
        > "$LOGS/xserver.autoreconf.log" 2>&1 || { tail -20 "$LOGS/xserver.autoreconf.log"; exit 1; }
    what=all
fi

if [ "$what" = configure ] || [ "$what" = all ]; then
    echo "==> configure"
    ./configure --host=$HOST --prefix=/X11 \
        --disable-shared --enable-static \
        --disable-xorg --disable-dmx --disable-xnest --disable-xquartz \
        --disable-xwin --disable-xwayland --disable-xephyr \
        --enable-kdrive --enable-xriscos --disable-xfake --disable-xfbdev \
        --disable-kdrive-kbd --disable-kdrive-mouse --disable-kdrive-evdev \
        --disable-tslib \
        --enable-xvfb \
        --disable-glx --disable-dri --disable-dri2 --disable-dri3 \
        --disable-libdrm --disable-glamor --disable-present \
        --disable-xshmfence --disable-mitshm \
        --disable-config-udev --disable-config-udev-kms --disable-config-hal \
        --disable-systemd-logind --disable-suid-wrapper \
        --disable-input-thread --disable-xselinux --disable-xcsecurity \
        --disable-xf86vidmode --disable-dga --disable-xf86bigfont \
        --disable-pciaccess --disable-int10-module --disable-vgahw \
        --disable-vbe --disable-linux-acpi --disable-linux-apm \
        --disable-xdmcp --disable-xdm-auth-1 --disable-secure-rpc \
        --disable-ipv6 --disable-unix-transport --disable-local-transport \
        --enable-tcp-transport \
        --disable-docs --disable-devel-docs --without-doxygen --without-xmlto \
        --without-fop --without-dtrace --disable-unit-tests \
        --with-sha1=libsha1 \
        --with-xkb-path=/X11/lib/X11/xkb \
        --with-xkb-output=/X11/lib/X11/xkb/compiled \
        --with-default-font-path=built-ins \
        > "$LOGS/xserver.configure.log" 2>&1 || { tail -40 "$LOGS/xserver.configure.log"; exit 1; }
    grep -E '^(checking for (fork|vfork|mmap|getpeereid|strndup|reallocarray|poll|epoll|ppoll|sigaction|setitimer)\b|config.status: creating Makefile$)' \
        "$LOGS/xserver.configure.log" || true
fi

if [ "$what" = make ] || [ "$what" = all ]; then
    echo "==> make"
    make -j"$(nproc)" -k CPPFLAGS="$COMPAT_CPPFLAGS" LDFLAGS="-L$PREFIX/lib -all-static" LIBS="-lrwl" > "$LOGS/xserver.make.log" 2>&1 || {
        grep -E 'error:|Error [0-9]|undefined reference' "$LOGS/xserver.make.log" | sort | uniq -c | sort -rn | head -60
        exit 1; }
    ls -l hw/vfb/Xvfb hw/kdrive/riscos/Xriscos
fi
