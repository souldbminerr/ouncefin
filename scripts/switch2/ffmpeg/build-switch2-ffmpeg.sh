#!/bin/sh
# Build Switch 2 FFmpeg (7.1.5) with the T239 viddec hardware decoders.
# Runs OUT OF TREE from the vendored sources at library/ffmpeg, so the
# vendored tree stays pristine (reconstruct it with vendor-ffmpeg.sh).
# Installs to $DEVKITPRO/portlibs/switch2 (overlaying switch1 portlibs).
# Run in msys2: source /opt/devkitpro/switch2vars.sh first.
set -e
: "${DEVKITPRO:?Set DEVKITPRO (source switch2vars.sh in msys2)}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/library/ffmpeg"
BUILD=${BUILD:-$ROOT/build_switch2/ffmpeg}
mkdir -p "$BUILD"
cd "$BUILD"

# Mirrors the switch port: target-os horizon, cortex-a78c, static,
# viddec needs the switch2 ffmpeg headers only present in this prefix.
"$SRC/configure" --prefix="$PORTLIBS_PREFIX" --enable-gpl --disable-shared --enable-static \
  --cross-prefix=aarch64-none-elf- --enable-cross-compile \
  --arch=aarch64 --cpu=cortex-a78c --target-os=horizon --enable-pic \
  --extra-cflags='-D__SWITCH__ -D__SWITCH2__ -D_GNU_SOURCE -O2 -mcpu=cortex-a78c+crypto -mtp=soft -fPIC -ftls-model=local-exec' \
  --extra-cxxflags='-D__SWITCH__ -D__SWITCH2__ -D_GNU_SOURCE -O2 -mcpu=cortex-a78c+crypto -mtp=soft -fPIC -ftls-model=local-exec' \
  --extra-ldflags="-fPIE -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/portlibs/switch/lib -L${DEVKITPRO}/libounce/lib -lounce" \
  --disable-runtime-cpudetect --disable-programs --disable-debug --disable-doc \
  --enable-asm --enable-neon --disable-autodetect --enable-mbedtls --enable-version3 \
  --disable-avdevice --disable-encoders --disable-muxers \
  --enable-swscale --enable-swresample --enable-network \
  --enable-zlib --enable-bzlib --enable-libass --enable-libdav1d \
  --disable-filters --enable-filter=hflip,vflip,transpose
make -j"$(nproc)"
make install
rm -rf "${PORTLIBS_PREFIX}/share"
echo "switch2 ffmpeg with viddec installed to $PORTLIBS_PREFIX (built from $SRC in $BUILD)"
