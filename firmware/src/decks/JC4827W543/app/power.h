#pragma once
// Deck power modes (UI task only).
//  - screen off: backlight off, rendering paused, Wi-Fi and telemetry keep running; any touch turns
//    it back on (that touch never reaches the widgets), and so does the BOOT button.
//  - deep sleep: Wi-Fi off, panel SLPIN, ESP32-S3 deep sleep; waking is a full restart (~3 s).
//    Wake sources are chosen at sleep time from the idle level of each line: the sleep button on
//    IO17/IO18 (header), the GT911 INT line (GPIO3, a touch) and the BOOT button (GPIO0).
//  - sleep button (IO17/IO18, e.g. a TTP223 touch-button module): a tap runs the action chosen in
//    Settings (deep sleep, or screen off/on); a tap while asleep wakes the deck.
//  - screensaver (ui::screensaver): while it is enabled it replaces auto deep sleep. BOOT closes it;
//    the sleep button closes it and then runs its action; screen off and deep sleep close it.
#include <stdint.h>

namespace power {

void init();               // call early in setup(): releases pins held during deep sleep, arms the button
void poll(uint32_t now);   // UI task: BOOT / sleep button, touch-INT diagnostics
void screen_off();
void screen_on();
bool is_screen_off();
[[noreturn]] void deep_sleep();
bool woke_from_deep_sleep();
const char *wake_reason();  // "touch", "button", "boot", "" (cold boot)
uint32_t touch_int_lows();  // diagnostics: samples with the touch INT line low
// Diagnostics: learnt idle level of each sleep-button pin (0/1) and taps seen since boot.
void button_info(int &idle_a, int &idle_b, uint32_t &taps);

}  // namespace power
