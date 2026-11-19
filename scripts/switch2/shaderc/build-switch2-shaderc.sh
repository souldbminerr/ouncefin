#!/bin/sh
# Cross-build the shaderc chain (spirv-headers -> spirv-tools ->
# glslang -> shaderc) for Switch 2 (aarch64-none-elf/Horizon) and
# install to $DEVKITPRO/portlibs/switch2. mpv's deko3d backend uses it
# to compile vo_gpu's GLSL to SPIR-V at runtime
# (GLSL -> shaderc -> SPIR-V -> uam2rt -> DKSH).
#
# Mirrors the mingw64 packaging in scripts/mingw64/{spirv-headers,
# spirv-tools,glslang,shaderc}: same versions, flags, de-vendor seds.
# Run in msys2: source /opt/devkitpro/switch2vars.sh first.
set -e
: "${DEVKITPRO:?Set DEVKITPRO (source switch2vars.sh in msys2)}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PREFIX="$DEVKITPRO/portlibs/switch2"
# Native-style path: cmake embeds absolute paths in generated build
# artifacts (e.g. PCH includes), which the native Windows cross-gcc
# cannot resolve when they are MSYS paths (/tmp, /c/...).
WORK=${WORK:-/c/devkitPro/msys2/tmp/shaderc-sw2}
# Bound compile parallelism: spirv-tools at -O3 needs ~1.5GB per
# translation unit; running unbounded alongside other builds OOMs.
JOBS=${JOBS:-4}
SPV_VER=1.4.363.0
GLSLANG_VER=16.6.0
SHADERC_VER=2026.4

mkdir -p "$WORK"
cd "$WORK"

cat > toolchain.cmake <<'EOF'
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-none-elf-gcc)
set(CMAKE_CXX_COMPILER aarch64-none-elf-g++)
set(CMAKE_AR aarch64-none-elf-ar)
set(CMAKE_RANLIB aarch64-none-elf-ranlib)
set(CMAKE_FIND_ROOT_PATH "$ENV{DEVKITPRO}/portlibs/switch2")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
EOF

fetch() { # fetch <url> <tar>
    [ -e "$2" ] || curl -sL -o "$2" "$1"
    tar xf "$2"
}

echo "== spirv-headers $SPV_VER"
fetch "https://github.com/KhronosGroup/SPIRV-Headers/archive/vulkan-sdk-$SPV_VER/sh.tar.gz" \
      "spirv-headers-$SPV_VER.tar.gz"
cmake -B hdr-build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$WORK/toolchain.cmake" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      "SPIRV-Headers-vulkan-sdk-$SPV_VER" >/dev/null
cmake --build hdr-build >/dev/null && cmake --install hdr-build >/dev/null

echo "== spirv-tools $SPV_VER"
fetch "https://github.com/KhronosGroup/SPIRV-Tools/archive/vulkan-sdk-$SPV_VER/st.tar.gz" \
      "spirv-tools-$SPV_VER.tar.gz"
cmake -B st-build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$WORK/toolchain.cmake" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_BUILD_TYPE=Release \
      -DSPIRV_WERROR=OFF \
      -DBUILD_SHARED_LIBS=OFF \
      -DSPIRV_TOOLS_BUILD_STATIC=ON \
      -DSPIRV_SKIP_EXECUTABLES=ON \
      -DSPIRV_SKIP_TESTS=ON \
      -DSPIRV-Headers_SOURCE_DIR="$PREFIX" \
      "SPIRV-Tools-vulkan-sdk-$SPV_VER" >/dev/null
cmake --build st-build -j "$JOBS" && cmake --install st-build >/dev/null

echo "== glslang $GLSLANG_VER"
fetch "https://github.com/KhronosGroup/glslang/archive/$GLSLANG_VER/g.tar.gz" \
      "glslang-$GLSLANG_VER.tar.gz"
cmake -B gl-build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$WORK/toolchain.cmake" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
      -DBUILD_SHARED_LIBS=OFF \
      -DENABLE_GLSLANG_BINARIES=OFF \
      -DGLSLANG_TESTS=OFF \
      -DBUILD_EXTERNAL=OFF \
      -DENABLE_OPT=OFF \
      "glslang-$GLSLANG_VER" >/dev/null
cmake --build gl-build -j "$JOBS" && cmake --install gl-build >/dev/null
# glslang's install skips the SPIRV/ headers (installed lib only);
# shaderc_util includes SPIRV/GlslangToSpv.h - carry them over.
mkdir -p "$PREFIX/include/SPIRV"
cp "$WORK"/glslang-"$GLSLANG_VER"/SPIRV/*.h "$PREFIX/include/SPIRV/"

echo "== shaderc $SHADERC_VER"
fetch "https://github.com/google/shaderc/archive/v$SHADERC_VER/s.tar.gz" \
      "shaderc-$SHADERC_VER.tar.gz"
cd "shaderc-$SHADERC_VER"
# de-vendor (same as the mingw64 packaging) and stop git versioning
patch -Nbp1 -s < "$ROOT/scripts/mingw64/shaderc/0001-fix-glslang-hlsl-linking-order.patch"
sed '/glslc/d;/third_party/d' -i CMakeLists.txt
cd "$WORK"
cmake -B sc-build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$WORK/toolchain.cmake" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=OFF \
      -Dglslang_SOURCE_DIR="$PREFIX/include" \
      -DSHADERC_SKIP_TESTS=ON \
      -DSHADERC_SKIP_EXAMPLES=ON \
      "shaderc-$SHADERC_VER" >/dev/null
cmake --build sc-build -j "$JOBS" && cmake --install sc-build >/dev/null

echo "shaderc chain installed to $PREFIX"
ls -la "$PREFIX/lib/libshaderc.a" "$PREFIX/lib/pkgconfig/shaderc.pc" 2>/dev/null
