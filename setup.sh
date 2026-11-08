#!/bin/bash
# Set up the build host: check the host tools, download and patch the upstream
# sources (fetch-sources.sh), build the xdev-gccsdk:1 Docker image, and fetch
# the GCCSDK sources that build-sul.sh needs for the SharedUnixLibrary module.
# Safe to re-run: each step is skipped when already done.
#
# Usage: setup.sh [--rebuild-image]
#
#   GCCSDK=<dir>   where to keep the GCCSDK git clone (default: ./gccsdk)
set -euo pipefail
cd "$(dirname "$0")"

IMAGE=xdev-gccsdk:1
GCCSDK=${GCCSDK:-$PWD/gccsdk}
GCCSDK_URL=https://github.com/riscos-dot-info/gccsdk.git
GCCSDK_COMMIT=625a8f03   # matches the Docker image's UnixLib; see build-sul.sh
SUL_PATH=gcc4/recipe/files/gcc/libunixlib

rebuild_image=
for a in "$@"; do
    case $a in
        --rebuild-image) rebuild_image=1 ;;
        -h|--help) sed -n '2,10s/^# \{0,1\}//p' "$0"; exit 0 ;;
        *) echo "unknown argument: $a" >&2; exit 2 ;;
    esac
done

step() { echo "==> $*"; }

step "checking host tools"
missing=()
for t in docker git wget sha256sum tar bzip2 gzip patch diff; do
    command -v "$t" >/dev/null || missing+=("$t")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "missing on this host: ${missing[*]}" >&2
    echo "(Debian/Ubuntu: apt install docker.io git wget coreutils tar bzip2 gzip patch diffutils)" >&2
    exit 1
fi
docker info >/dev/null 2>&1 || {
    echo "cannot talk to the Docker daemon: is it running, and are you in the 'docker' group?" >&2
    exit 1; }

step "fetching and patching sources"
./fetch-sources.sh

if [ -n "$rebuild_image" ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    step "building Docker image $IMAGE"
    docker build ${rebuild_image:+--pull} -t "$IMAGE" docker
else
    step "Docker image $IMAGE already present (--rebuild-image to redo)"
fi

if ! git -C "$GCCSDK" cat-file -e "$GCCSDK_COMMIT^{commit}" 2>/dev/null; then
    if [ -d "$GCCSDK/.git" ]; then
        step "updating GCCSDK clone in $GCCSDK"
        git -C "$GCCSDK" fetch --filter=blob:none origin
    else
        step "cloning GCCSDK into $GCCSDK (history only; files fetched as needed)"
        git clone --filter=blob:none --no-checkout "$GCCSDK_URL" "$GCCSDK"
    fi
fi
# Fetch the files build-sul.sh takes from the partial clone now, so the build
# itself needs no network.
git -C "$GCCSDK" archive "$GCCSDK_COMMIT" "$SUL_PATH" > /dev/null
step "GCCSDK $GCCSDK_COMMIT ready in $GCCSDK"

step "setup done; now run ./build.sh"
