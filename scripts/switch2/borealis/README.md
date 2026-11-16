# borealis Switch 2 (libounce + drako3d) support patch

`switch2-drako3d-support.patch` adds a first-class Switch 2 target to
borealis, so switchfin's top-level `CMakeLists.txt` no longer needs
include-order / pkg-config / link-swap workarounds. Generated against
upstream borealis `0303fe7f`; verify with `git apply --check` before use.

What it does (3 files, all under `library/` in the borealis repo):

- `library/cmake/commonOption.cmake`
  - new `option(PLATFORM_SWITCH2 ...)` and
    `cmake_dependent_option(USE_DRAKO3D ... "PLATFORM_SWITCH2")`;
  - `-Wl,-Map` link option also applies to `PLATFORM_SWITCH2`.
- `library/cmake/toolchain.cmake`
  - new `elseif (PLATFORM_SWITCH2)` branch *before* the Switch 1 branch
    (switchfin sets both, so order matters): `-D__SWITCH2__` plus
    libounce / switch2-portlibs / switch-portlibs `-I` flags, and it
    deliberately does NOT include devkitPro's `Switch.cmake`, which would
    reset `NX_ROOT` and `PKG_CONFIG_EXECUTABLE` to Switch 1 locations;
  - `USE_DRAKO3D` maps to `BOREALIS_USE_DEKO3D` (drako3d is API-compatible
    with deko3d; `deko3d.h/hpp` resolve via switchfin's
    `cmake/switch2/drako3d-compat` shims).
- `library/CMakeLists.txt`
  - `PLATFORM_SWITCH` source block also matches `PLATFORM_SWITCH2`;
  - links `drako3d` (Release) / `drako3dd` (Debug) instead of `deko3d`;
  - drops Switch 1 `drm_nouveau` from the SDL2/GLFW fallback libs;
  - nanovg-dk sources compile for `PLATFORM_SWITCH2` too.

## Apply to a fork

```sh
cd <borealis-fork>
git apply /path/to/switchfin/scripts/switch2/borealis/switch2-drako3d-support.patch
```

Then point switchfin at the fork (submodule/subtree) and drop the local
copies under `library/borealis`.
