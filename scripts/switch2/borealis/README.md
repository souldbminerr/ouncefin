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

---

`switch2-padmouse-support.patch` adds native Switch 2 Joy-Con mouse
support to `SwitchInputManager` (all `__SWITCH2__`-guarded, purely
additive — Switch 1 behavior is untouched). Generated against upstream
borealis `0303fe7f`; same apply procedure as above.

Behavior, all inside `library/.../switch/switch_input.{hpp,cpp}`:

- Built on libounce's native `PadPointer` API (`padPointerInitialize`,
  `padPointerUpdate`, `padPointerGetButtons`) — per-device Joy-Con
  handles. It deliberately never calls `hidSetMergedNmouseSourceTypes`:
  the merged pointer hands the Joy-Con sticks to the pointer service,
  which kills stick navigation on the pads. `PadPointer` leaves the
  sticks alone until a Joy-Con actually slides on a surface.
- Opt-in through `InputManager::setPadMouseEnabled()` (default no-op on
  other platforms). Switchfin wires it to the "Joy-Con mouse" settings
  toggle; with it off, no Nmouse service is initialized and the pads
  keep every button and stick.
- Engagement requires real pointer travel (64 px net from the anchor)
  or wheel scroll — never a bare click, so resting fingers cannot
  phantom-click and a press on an idle pointer stays a pad press.
- While mouse mode is on, L/R/ZL/ZR are pointer clicks for the whole
  mode, never pad buttons, and any real use (net travel, scroll, click)
  keeps the mode alive; 3 s without use ends it. Buttons the app bound
  through `InputManager::setPadMouseBindings()` (e.g. the player's seek
  keys) do not end mouse mode when pressed — they are mouse-mode input;
  every other controller button, touch or pointer loss hands control
  back to the pads. `InputManager::isPadMouseMode()` lets views bind
  alternate actions for that state, and `isPadMouseBinding()` keeps core
  input handling from dismissing those buttons' actions or flipping the
  input type while the mode is on. Switchfin's player uses it for
  X/DpadUp seek forward, DpadLeft/A seek back and Y/DpadRight for the
  settings side menu, only in mouse mode.
- The tap gesture treats right-click as the secondary tap and fires the
  view's `BUTTON_X` actions for it (touch taps stay `BUTTON_A`), so
  right-click works like the X button everywhere, including real USB
  mice. `processInput` revalidates the cached mouse view against the
  current content tree, so clicks after a navigation (dialogs,
  dropdowns, tabs) always hit-test fresh instead of dispatching to
  detached views.
- Pointer area is fixed at 1920x1080 and mapped to layout points, so
  the cursor and hit-testing stay correct in every UI scale, handheld
  and docked. Cursor is a semi-transparent white dot (50% idle, 75%
  while a click is held). There is no image cursor path and no ordinary
  (USB) mouse support on Switch 2: the Joy-Con pointer is the only mouse
  source, so nothing is left stuck on screen when the mouse is off.

---

`switch2-dk-zerocopy.patch` adds a zero-copy video upload path to the
deko3d/drako3d nanovg backend (4 files, ~110 lines, purely additive).
Generated against upstream borealis `0303fe7f`; same apply procedure
(`git apply --ignore-whitespace` on a CRLF checkout). What it does:

- `NVG_IMAGE_STREAMING` on the dk backend now gives a texture a
  persistent upload backing allocated from the dual-mapped (CPU +
  GPU) data pool, sized to the image and owned by the texture.
- `DkRenderer::UpdateTexture` recognizes an update whose data pointer
  is that exact backing and then issues only a GPU-side
  `copyBufferToImage` from the backing's GPU address — no staging
  memcpy and no `transferQueue.waitIdle()`. Command memory is pooled
  in a small ring recycled behind fences (nothing is freed while the
  GPU may still execute it; the wait before reuse is on a many-frames
  -old fence, so it is instant). Other callers keep the existing
  staged path.
- `DkRenderer::GetImageBacking` / `SwitchVideoContext::getImageBacking`
  expose the backing's CPU address to the application, which renders
  into it directly (switchfin's mpv SW render, ping-pong pair — see
  `app/src/view/mpv_core.cpp`).

