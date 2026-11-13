#pragma once
// Deck power modes (UI task only).
//  - screen off: backlight off, Wi-Fi and telemetry keep running; any touch turns it back on (that
//    touch never reaches the widgets).
//  - auto sleep (Settings > System) turns the screen off after N minutes without a touch, unless the
//    screensaver is enabled (it replaces auto sleep).
// The ESP32-4848S040 has no deep-sleep wake line (the GT911's INT is not wired) and no BOOT button at
// runtime (GPIO0 is a pixel data line), so there is no deep sleep: the screen turns off instead.
#include <stdint.h>

namespace power {

void init();              // call early in setup(): touch wake callback
void poll(uint32_t now);  // UI task (nothing to poll on this board)
void screen_off();
void screen_on();
bool is_screen_off();

}  // namespace power
