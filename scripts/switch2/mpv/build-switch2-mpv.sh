#!/bin/sh
# Build the Switch 2 libmpv with the deko3d (drako3d) GPU render backend.
# Runs OUT OF TREE from the vendored sources at library/mpv, so the
# vendored tree stays pristine (reconstruct it with vendor-mpv.sh).
# Installs to /opt/devkitpro/portlibs/switch2 (the SW-only libmpv is
# kept as libmpv_sw_backup.a the first time).
# Run in msys2: source /opt/devkitpro/switch2vars.sh first.
set -e
: "${DEVKITPRO:?Set DEVKITPRO (source switch2vars.sh in msys2)}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/library/mpv"
BUILD=${BUILD:-$ROOT/build_switch2/mpv}
PREFIX=/opt/devkitpro/portlibs/switch2

meson setup "$BUILD" "$SRC" \
    --cross-file "$ROOT/scripts/switch2-cross-mpv-gpu.ini" \
    -Dlibmpv=true -Dcplayer=false -Dtests=false -Dlua=disabled \
    -Ddeko3d=enabled -Ddeko3d_precompiled=true -Dgl=disabled \
    -Dshaderc=enabled \
    -Dsdl2=disabled -Dlibarchive=disabled -Dlibplacebo=disabled \
    -Ddefault_library=static
ninja -C "$BUILD"
[ -f "$PREFIX/lib/libmpv.a" ] && [ ! -f "$PREFIX/lib/libmpv_sw_backup.a" ] && \
    cp "$PREFIX/lib/libmpv.a" "$PREFIX/lib/libmpv_sw_backup.a"
meson configure "$BUILD" -Dprefix="$PREFIX"
meson install -C "$BUILD"
echo "switch2 libmpv (deko3d GPU backend) installed to $PREFIX (built from $SRC in $BUILD)"
