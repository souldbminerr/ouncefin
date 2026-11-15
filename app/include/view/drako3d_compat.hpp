#pragma once

/* Switch 2 drako3d only */

#if defined(BOREALIS_USE_DRAKO3D) && !defined(__SWITCH2__)
#error "BOREALIS_USE_DRAKO3D requires __SWITCH2__ (PLATFORM_SWITCH2)"
#endif

namespace switch2 {
inline constexpr const char* RENDER_BACKEND = "drako3d";
inline constexpr const char* VIDEO_BACKEND = "FFmpeg";
}  // namespace switch2
