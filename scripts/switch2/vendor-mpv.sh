#!/bin/sh
# Reconstruct the vendored mpv tree at library/mpv: pristine 0.36.0 +
# the Switch 1 deko3d backend (upstream wiliwili patch) + the Switch 2
# drako3d changes (precompiled-shader loader keyed by GLSL hash,
# romfs-first lookup, dump-on-miss, nvtegra hwdec exclusion). Sources
# only: meson builds out of tree.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MPV="$ROOT/library/mpv"
TARBALL=${1:?usage: vendor-mpv.sh <mpv-0.36.0.tar.gz>}

rm -rf "$MPV"
mkdir -p "$MPV"
tar xzf "$TARBALL" -C "$MPV" --strip-components=1
cd "$MPV"

# The deko3d-switch2 patch is generated against pristine+S1; the S1
# patch must be applied first.
patch -Np1 --silent < "$ROOT/scripts/switch/mpv/deko3d.patch"
patch -Np1 --silent < "$ROOT/scripts/switch2/mpv/deko3d-switch2.patch"

echo "vendored mpv ready: $MPV"
