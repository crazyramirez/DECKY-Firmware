#pragma once
// Deck power modes (UI task only).
//  - screen off: backlight + panel off, rendering paused, Wi-Fi and telemetry keep running;
//    any touch turns it back on (that touch never reaches the widgets).
//  - auto sleep (Settings > System) goes to deep sleep after N minutes without a touch, unless the
//    screensaver is enabled (it replaces auto sleep). BOOT, screen off and deep sleep close it.
//  - deep sleep: Wi-Fi off, backlight held off, ESP32-S3 deep sleep. Wakes on the touch controller's
//    INT line (GPIO3, active low) or the BOOT button (GPIO0); waking is a full restart (~3 s).
//    A touch wake is checked first (wake_touch::boot_check): no finger on the glass → straight back
//    to sleep, backlight never on; the same quick tap repeated within 4 s wakes it.
//  - screen off: only a real press wakes it (wake_touch::Filter: held still ~60 ms).
//    The panel is NOT put in SLPIN (PCD_DEEP_SLEEP_PANEL_SLPIN=1 restores it): the AXS15231B's touch
//    core shares the chip and only scans slowly in SLPIN, so waking took several taps.
#include <stdint.h>

namespace power {

void init();               // call early in setup(): releases pins held during deep sleep
void poll(uint32_t now);   // UI task; touch-INT diagnostics
void screen_off();
void screen_on();
bool is_screen_off();
[[noreturn]] void deep_sleep();
bool woke_from_deep_sleep();
uint32_t touch_int_lows();  // diagnostics: samples with the touch INT line low (should rise while touching)

}  // namespace power
