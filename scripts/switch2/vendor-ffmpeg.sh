#!/bin/sh
# Reconstruct the vendored FFmpeg tree at library/ffmpeg:
# pristine 7.1.5 + horizon support + aligned-alloc backport + the T239
# viddec decoders + codec registration.
#
# The viddec decoders are canonical in the vendored tree itself
# (library/ffmpeg/libavcodec/viddec*.c). When re-vendoring (e.g. an
# ffmpeg version bump), the current viddec sources and any other local
# changes must be carried over deliberately: this script stashes the
# viddec files before wiping the tree and restores them afterwards.
# Other uncommitted edits are lost - commit first.
#
# Sources only: configure/make run out of tree
# (scripts/switch2/ffmpeg/build-switch2-ffmpeg.sh).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FF="$ROOT/library/ffmpeg"
TARBALL=${1:?usage: vendor-ffmpeg.sh <ffmpeg-7.1.5.tar.xz>}

STASH=$(mktemp -d)
for f in viddec.h viddec.c viddec_h264.c viddec_vp9.c viddec_av1.c viddec_vp8.c; do
    [ -e "$FF/libavcodec/$f" ] && cp "$FF/libavcodec/$f" "$STASH/"
done

rm -rf "$FF"
mkdir -p "$FF"
tar xJf "$TARBALL" -C "$FF" --strip-components=1
cd "$FF"

patch -Np1 --silent < "$ROOT/scripts/switch2/ffmpeg/horizon.patch"
patch -Np1 --silent < "$ROOT/scripts/switch2/ffmpeg/buffer_aligned_alloc.patch"

cp "$STASH"/viddec* libavcodec/ 2>/dev/null || \
    echo "WARNING: no stashed viddec sources; copy them in before building" >&2
rm -rf "$STASH"

# Register the decoders (Makefile objects + allcodecs externs, picked
# up by configure's find_things_extern).
grep -q viddec_h264 libavcodec/Makefile || cat >> libavcodec/Makefile <<'EOF'
OBJS-$(CONFIG_H264_VIDDEC_DECODER)       += viddec.o viddec_h264.o
OBJS-$(CONFIG_VP9_VIDDEC_DECODER)        += viddec.o viddec_vp9.o
OBJS-$(CONFIG_AV1_VIDDEC_DECODER)        += viddec.o viddec_av1.o
OBJS-$(CONFIG_VP8_VIDDEC_DECODER)        += viddec.o viddec_vp8.o
EOF

for c in h264 vp9 av1 vp8; do
    grep -q "${c}_viddec" libavcodec/allcodecs.c || \
        sed -i "s|extern const FFCodec ff_${c}_decoder;|extern const FFCodec ff_${c}_decoder;\nextern const FFCodec ff_${c}_viddec_decoder;|" libavcodec/allcodecs.c
done

echo "vendored ffmpeg ready: $FF"
