#!/bin/sh
# Build Switchfin for Switch 2 (libounce + OunceVK, software decode).
# Requires: devkitPro + ounce-devkit (source switch2vars.sh first or set DEVKITPRO).
set -e
if [ -z "$DEVKITPRO" ]; then
  echo "error: DEVKITPRO not set. Install devkitPro pacman + ounce-devkit:" >&2
  echo "  https://github.com/ouncebrew/ounce-devkit (sh install.sh)" >&2
  exit 1
fi
if [ -f "$DEVKITPRO/switch2vars.sh" ]; then
  # shellcheck disable=SC1090
  . "$DEVKITPRO/switch2vars.sh"
else
  echo "warning: \$DEVKITPRO/switch2vars.sh not found; continuing with current env" >&2
fi
# USE_DRAKO3D selects the drako3d GPU path (borealis deko3d code compiled
# against drako3d via cmake/switch2/drako3d-compat). Omit it for the
# OunceVK OpenGL fallback.
aarch64-none-elf-cmake -S . -B build_switch2 \
  -DPLATFORM_SWITCH2=ON \
  -DUSE_DRAKO3D=ON \
  -DMPV_SW_RENDER=OFF \
  -DCMAKE_BUILD_TYPE=Release -G Ninja "$@"
cmake --build build_switch2 -j"$(nproc 2>/dev/null || echo 4)"
echo "done: build_switch2/Switchfin.elf (+ Switchfin.nro if packaging succeeded)"
