#pragma once
// Deck power modes (UI task only).
//  - screen off: backlight off, rendering paused, Wi-Fi and telemetry keep running; any touch turns
//    it back on (that touch never reaches the widgets), and so does the BOOT button.
//  - auto sleep (Settings > System) turns the screen off after N minutes without a touch, unless the
//    screensaver is enabled (it replaces auto sleep). BOOT closes the screensaver.
// The JC1060P470C has no deep-sleep wake line (touch INT not wired, BOOT is not an LP GPIO), so there
// is no deep sleep: RST restarts the board.
#include <stdint.h>

namespace power {

void init();              // call early in setup(): BOOT button input, touch wake callback
void poll(uint32_t now);  // UI task: BOOT button (toggles the screen)
void screen_off();
void screen_on();
bool is_screen_off();
uint32_t boot_presses();  // diagnostics

}  // namespace power
