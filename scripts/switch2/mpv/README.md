# Switch 2 libmpv with the deko3d (drako3d) GPU render backend

mpv's `vo_gpu` uploads the decoded NV12 once and does conversion,
scaling, interpolation, subtitles and OSD **entirely on the GPU**, and
renders straight into the app's swapchain (the `mpv_deko3d_fbo` path in
`app/src/view/mpv_core.cpp`). That replaces the CPU swscale pipeline
(MPV_SW_RENDER) — no per-frame RGBA conversion, no CPU copies.

## Why the patch exists

Switch 1 compiles vo_gpu's GLSL at runtime through libuam, which emits
Maxwell microcode — the T239 (Ampere) GPU rejects it, and the Switch 2
toolchain has no on-device GLSL compiler. `deko3d-switch2.patch` therefore
replaces the `uam_*` calls in `video/out/deko3d/ra_dk.c` with:

- a precompiled-shader **loader** keyed by an FNV-1a hash of the GLSL
  sources, serving `dk_shadercache` blobs (the format mpv already
  exchanges through vo_gpu's `cached_program`) from
  `sdmc:/switch/Switchfin/shaders/<hash>.bin`
- a **dump-on-miss**: the requested GLSL is written to
  `sdmc:/switch/Switchfin/shader-dump/<hash>.{vert,frag,comp}.glsl`

`meson.build` gains a `deko3d_precompiled` option: it links drako3d
instead of deko3d, needs no libuam, and skips the Switch 1 NVDEC hwdec
bridge (`hwdec_deko3d.c`; Switch 2 decodes through the FFmpeg viddec
decoders instead).

## Build (msys2, source switch2vars.sh)

The backend sources are **vendored in-tree** (`library/mpv`, already
carrying the S1 deko3d patch and the Switch 2 changes; reconstruct with
`scripts/switch2/mpv/vendor-mpv.sh` if ever needed). One script:

```sh
sh scripts/switch2/mpv/build-switch2-mpv.sh
```

(out of tree from `library/mpv` into `build_switch2/mpv`, installs to
`/opt/devkitpro/portlibs/switch2`, keeps the SW-only lib as
`libmpv_sw_backup.a`). The cross file
(`scripts/switch2-cross-mpv-gpu.ini`) adds the in-repo
`cmake/switch2/drako3d-compat` include (the `<deko3d.h>` shims) and
`-ldrako3d`. Then rebuild switchfin (`scripts/build-switch2.sh`, which
passes `-DMPV_SW_RENDER=OFF`).

## Shader capture (one-time, on hardware — then shipped)

The app pins the shader-affecting options after `mpv_initialize`
(bilinear scalers, no deband, no interpolation, no temporal dithering),
so with viddec's NV12-only output the variant space is small and
closed: video colorspace passes plus OSD/subtitle blends.

1. Run the app and play a video. Video will not show until the blobs
   exist; the log lists
   `deko3d: no precompiled shader <hash> ... dumped GLSL to ...`.
2. Copy `sdmc:/switch/Switchfin/shader-dump/` to the PC and run
   `sh scripts/switch2/mpv/build-mpv-shaders.sh <dump_dir> <out_dir>`
   (uam2 compiles each GLSL for SM 8.6).
3. **Ship them: copy `<out_dir>/*.bin` to the repo's
   `resources/mpv-shaders/`** — they are embedded as
   `romfs:/mpv-shaders/`, which ra_dk tries before the SD card, so
   users get working video out of the box. The SD directory
   (`sdmc:/switch/Switchfin/shaders/`) remains a runtime
   override/extension path.
4. Repeat with varied content (60fps, subtitles, seek OSD) until no
   more dump lines appear.

The blobs use the same layout mpv's own `--gpu-shader-cache` persists,
so `load_shader_code` consumes them unchanged. If a future build
changes the pinned options (or mpv generates a new variant), the same
capture flow adds it.
