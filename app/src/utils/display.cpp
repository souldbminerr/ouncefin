#include "utils/display.hpp"

#ifdef __SWITCH2__
#include <switch.h>
#include <borealis.hpp>

namespace {
// S2 ONLY: handheld panel is 1920x1080, docked output is 3840x2160.
// Used only when appletGetDefaultDisplayResolution fails.
constexpr int HANDHELD_W = 1920, HANDHELD_H = 1080;
constexpr int DOCKED_W = 3840, DOCKED_H = 2160;

AppletHookCookie s_hookCookie;
bool s_hookInstalled = false;
int s_lastFpsMode = -1;

void s_onAppletHook(AppletHookType hook, void* /*arg*/) {
    if (hook == AppletHookType_OnOperationMode) {
        // Dock/undock: resolution changes
        Switch2Display::update();
    }
}
}  // namespace

void Switch2Display::update() {
    const bool docked = (appletGetOperationMode() == AppletOperationMode_Console);
    const int fpsMode = docked ? 1 : 0;
    if (fpsMode != s_lastFpsMode) {
        Result rc = docked ? nwindowSetFrameRates(nwindowGetDefault(), 0, 0, false)
                           : nwindowSetFrameRates(nwindowGetDefault(), 120, 60, false);
        if (R_SUCCEEDED(rc)) {
            s_lastFpsMode = fpsMode;
        } else {
            brls::Logger::warning("Switch2 display: nwindowSetFrameRates failed: 0x%x", (unsigned)rc);
        }
    }

    int32_t w = 0, h = 0;
    if (R_SUCCEEDED(appletGetDefaultDisplayResolution(&w, &h)) && w > 0 &&
        h > 0) {
        // Real display resolution: 1080p handheld, 4K docked.
    } else if (docked) {
        w = DOCKED_W;
        h = DOCKED_H;
    } else {
        w = HANDHELD_W;
        h = HANDHELD_H;
    }

    if (w == (int32_t)brls::Application::windowWidth &&
        h == (int32_t)brls::Application::windowHeight)
        return;

    brls::Logger::info("Switch2 display: {}x{} (mode={})", w, h,
        (int)appletGetOperationMode());
    brls::Application::onWindowResized(w, h);
}

void Switch2Display::init() {
    // Push the real resolution now (SDL/GLFW windows start at layout size).
    update();
    if (!s_hookInstalled) {
        appletHook(&s_hookCookie, s_onAppletHook, nullptr);
        s_hookInstalled = true;
    }
}

#else  // !__SWITCH2__

void Switch2Display::update() {}
void Switch2Display::init() {}

#endif
