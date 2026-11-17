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

- Uses the raw `HidNmouse` API (`hidInitializeNmouse`, merged Joy-Con
  handle, `hidGetNmouseStatesWithDelta` — the same pattern as
  moonlight-nx2's proven `platform.c`, with OpenRCT2-Switch's button
  config: triggers ZL/ZR = left-click group, shoulders L/R = right-click
  group). A Joy-Con in mouse position drives `RawMouseState`, so tap
  (click), pan (drag) and scroll (Joy-Con stick = wheel) gestures work;
  clicks themselves come straight from live pad polling (ZL = left,
  ZR = right), so no service mapping can double-fire or drop them.
  Adopt-on-engage keeps resting fingers from phantom-clicking, and
  engagement requires tracked sensor movement (64 px net, 256 px
  fallback). `processInput` revalidates the cached mouse view against the
  current content tree, so clicks after a navigation (dialogs, dropdowns,
  tabs) always hit-test fresh instead of dispatching to detached views.
- Pointer area is fixed at 1920x1080 and mapped to layout points, so the
  cursor and hit-testing stay correct in every UI scale, handheld and docked.
- Controller/mouse modes: movement of 64 px or more, a pointer click, or
  scrolling enters mouse mode; any controller button press, pointer loss,
  or 3 s without mouse use returns to controller mode. In mouse mode
  ZL/ZR are masked from the pad (they report `HidMouseButton_Left`, i.e.
  click, by default) instead of triggering controller actions.
- Cursor is a semi-transparent white dot (35% idle, 65% while the left
  button is held) drawn in mouse mode even while clicking; the USB-mouse
  image path is unchanged.
- Context click: `setPadMouseRightClick()` (default right) maps pad ZL/ZR
  to right- or left-click; the player opts into left-click. The tap
  gesture treats right-click as the secondary tap and fires the view's
  `BUTTON_X` actions for it (touch taps stay `BUTTON_A`), so right-click
  works like the X button everywhere, including real USB mice.
- Mouse-mode bindings: `InputManager::isMouseModeBinding()` (default
  `false`, so other platforms are untouched) reports buttons the app bound
  to mouse mode. Switch overrides it from the exit-exempt mask
  (`setMouseModeExitExempt()`); `handleAction` neither dismisses nor
  gamepad-flips such buttons, so consuming them never hides the cursor.
  Switchfin's video player uses this for mouse-mode-only seeks (X/DpadUp
  forward, A/DpadLeft back); see `VideoView` in the main repo.
