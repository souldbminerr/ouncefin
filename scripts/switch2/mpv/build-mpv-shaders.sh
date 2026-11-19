#!/bin/sh
# Compile mpv deko3d shader dumps (Switch 2) and assemble the
# dk_shadercache blobs that ra_dk.c loads from
# sdmc:/switch/Switchfin/shaders.
#
# The Switch 2 libmpv has no on-device shader compiler (libuam emits
# Maxwell microcode; the T239 is Ampere). On the first run mpv dumps the
# GLSL it wants to sdmc:/switch/Switchfin/shader-dump/ as
# <hash>.{vert,frag,comp}.glsl. Copy that directory to the PC and run:
#
#   sh build-mpv-shaders.sh <dump_dir> <out_dir>
#
# then copy the resulting <out_dir>/*.bin to
# sdmc:/switch/Switchfin/shaders/ on the SD card. Re-run the app; more
# dumps may appear (OSD/subtitle passes); iterate until no
# "no precompiled shader" lines remain in the log.
#
# Blob layout = the format save_shader_code/load_shader_code exchange
# (struct dk_shadercache_hdr, 0x20 bytes):
#   u32 magic "DKCH" | i32 version=1 |
#   u32 vertex_offset, vertex_size | u32 fragment_offset, fragment_size |
#   u32 compute_offset, compute_size
# followed by the whole uam2 .dksh per stage (control block embedded,
# as dkShaderMakerDefaults with control == NULL expects).
set -e

UAM2=${UAM2:-/opt/devkitpro/tools/bin/uam2.exe}
DUMP=${1:?usage: build-mpv-shaders.sh <dump_dir> <out_dir>}
OUT=${2:?usage: build-mpv-shaders.sh <dump_dir> <out_dir>}
mkdir -p "$OUT"

pack_header() {
    # $1..$8: vertex_off vertex_size fragment_off fragment_size compute_off compute_size
    printf 'DKCH' > "$TMPHDR"
    printf '\x01\x00\x00\x00' >> "$TMPHDR"                       # cache_version
    for v in "$@"; do printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((v & 255)) $(((v >> 8) & 255)) $(((v >> 16) & 255)) $((v >> 24 & 255)))" >> "$TMPHDR"; done
}

TMPHDR=$(mktemp)
trap 'rm -f "$TMPHDR"' EXIT

for f in "$DUMP"/*.vert.glsl; do
    [ -e "$f" ] || continue
    h=$(basename "$f" .vert.glsl)
    echo "raster pass $h"
    "$UAM2" -s vert -o "$OUT/$h.vert.dksh" "$f"
    [ -e "$DUMP/$h.frag.glsl" ] || { echo "missing $h.frag.glsl" >&2; continue; }
    "$UAM2" -s frag -o "$OUT/$h.frag.dksh" "$DUMP/$h.frag.glsl"
    vsz=$(wc -c < "$OUT/$h.vert.dksh")
    fsz=$(wc -c < "$OUT/$h.frag.dksh")
    # Offsets use raw (unaligned) sizes, like save_shader_code.
    pack_header 32 "$vsz" $((32 + vsz)) "$fsz" 0 0
    cat "$TMPHDR" "$OUT/$h.vert.dksh" "$OUT/$h.frag.dksh" > "$OUT/$h.bin"
    rm -f "$OUT/$h.vert.dksh" "$OUT/$h.frag.dksh"
done

for f in "$DUMP"/*.comp.glsl; do
    [ -e "$f" ] || continue
    h=$(basename "$f" .comp.glsl)
    echo "compute pass $h"
    "$UAM2" -s comp -o "$OUT/$h.comp.dksh" "$f"
    csz=$(wc -c < "$OUT/$h.comp.dksh")
    pack_header 0 0 0 0 32 "$csz"
    cat "$TMPHDR" "$OUT/$h.comp.dksh" > "$OUT/$h.bin"
    rm -f "$OUT/$h.comp.dksh"
done

echo "done: $(ls "$OUT" | grep -c '\.bin$') blob(s) in $OUT"
