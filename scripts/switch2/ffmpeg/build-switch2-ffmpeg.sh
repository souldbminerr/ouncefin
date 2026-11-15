#!/usr/bin/env bash
# Build Switch 2 FFmpeg (7.1.5) with viddec hardware decoders.
# Run in msys2: source /opt/devkitpro/switch2vars.sh first.
# Installs to $DEVKITPRO/portlibs/switch2 (overlaying switch1 portlibs).
set -e
: "${DEVKITPRO:?Set DEVKITPRO (source switch2vars.sh in msys2)}"
FFVER=7.1.5
SRC=/tmp/ffmpeg-${FFVER}-sw2
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
mkdir -p /tmp
if [ ! -d "$SRC" ]; then
  curl -sL "https://ffmpeg.org/releases/ffmpeg-${FFVER}.tar.xz" -o /tmp/ffmpeg-${FFVER}.tar.xz
  mkdir -p "$SRC"
  tar xJf /tmp/ffmpeg-${FFVER}.tar.xz -C "$SRC" --strip-components=1
fi
# Horizon OS support (from switch port: target-os + sysctl disable).
patch -Np1 -d "$SRC" --forward --silent < "$REPO/scripts/switch2/ffmpeg/horizon.patch" 2>/dev/null || true
# Backport av_buffer_aligned_alloc (needed by mpv 0.36 mp_image.c).
patch -Np1 -d "$SRC" --forward --silent < "$REPO/scripts/switch2/ffmpeg/buffer_aligned_alloc.patch" 2>/dev/null || true
# Copy viddec sources.
cp "$REPO/scripts/switch2/ffmpeg/viddec.h" "$SRC/libavcodec/viddec.h"
cp "$REPO/scripts/switch2/ffmpeg/viddec.c" "$SRC/libavcodec/viddec.c"
cp "$REPO/scripts/switch2/ffmpeg/viddec_h264.c" "$SRC/libavcodec/viddec_h264.c"
cp "$REPO/scripts/switch2/ffmpeg/viddec_vp9.c" "$SRC/libavcodec/viddec_vp9.c"
cp "$REPO/scripts/switch2/ffmpeg/viddec_av1.c" "$SRC/libavcodec/viddec_av1.c"
cp "$REPO/scripts/switch2/ffmpeg/viddec_vp8.c" "$SRC/libavcodec/viddec_vp8.c"
# Register in Makefile (idempotent append).
grep -q viddec_h264 "$SRC/libavcodec/Makefile" || cat >> "$SRC/libavcodec/Makefile" <<'EOF'
OBJS-$(CONFIG_H264_VIDDEC_DECODER)       += viddec.o viddec_h264.o
OBJS-$(CONFIG_VP9_VIDDEC_DECODER)        += viddec.o viddec_vp9.o
OBJS-$(CONFIG_AV1_VIDDEC_DECODER)        += viddec.o viddec_av1.o
OBJS-$(CONFIG_VP8_VIDDEC_DECODER)        += viddec.o viddec_vp8.o
EOF
# Register in allcodecs.c (idempotent, before wmavoice block for stability).
grep -q h264_viddec "$SRC/libavcodec/allcodecs.c" || \
  sed -i 's|extern const FFCodec ff_h264_decoder;|extern const FFCodec ff_h264_decoder;\nextern const FFCodec ff_h264_viddec_decoder;|' "$SRC/libavcodec/allcodecs.c"
grep -q vp9_viddec "$SRC/libavcodec/allcodecs.c" || \
  sed -i 's|extern const FFCodec ff_vp9_decoder;|extern const FFCodec ff_vp9_decoder;\nextern const FFCodec ff_vp9_viddec_decoder;|' "$SRC/libavcodec/allcodecs.c"
grep -q av1_viddec "$SRC/libavcodec/allcodecs.c" || \
  sed -i 's|extern const FFCodec ff_av1_decoder;|extern const FFCodec ff_av1_decoder;\nextern const FFCodec ff_av1_viddec_decoder;|' "$SRC/libavcodec/allcodecs.c"
grep -q vp8_viddec "$SRC/libavcodec/allcodecs.c" || \
  sed -i 's|extern const FFCodec ff_vp8_decoder;|extern const FFCodec ff_vp8_decoder;\nextern const FFCodec ff_vp8_viddec_decoder;|' "$SRC/libavcodec/allcodecs.c"
# Configure (mirrors switch PKGBUILD, retargeted to Switch 2).
cd "$SRC"
source /opt/devkitpro/switch2vars.sh
./configure --prefix="$PORTLIBS_PREFIX" --enable-gpl --disable-shared --enable-static \
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
make -j8
make install
rm -rf "${PORTLIBS_PREFIX}/share"
echo "switch2 ffmpeg with viddec installed to $PORTLIBS_PREFIX"
