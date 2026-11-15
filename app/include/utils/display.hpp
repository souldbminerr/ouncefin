#pragma once

/* Switch 2 display manager. Handheld 1080p Docked 2160p/4k */
class Switch2Display {
public:
    /// Get the real display resolution and give it to borealis
    static void update();

    /// Install the docking hook. Call once after window creation.
    static void init();
};
