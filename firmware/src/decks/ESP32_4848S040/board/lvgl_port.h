#pragma once
#include <stdint.h>

/*
 * LVGL <-> board glue. Threading rule: every lv_* call happens in the Arduino loop task.
 * Other tasks (network, timers) must post to the app event queue instead of touching widgets.
 *
 * Render strategy: LVGL "direct mode" into its own full frame (PSRAM). It draws an area in layers
 * (what lies underneath first, then the top layer, keyboards...), so it never draws into the frame
 * the RGB panel is scanning: at the end of a refresh lcd::present() copies the finished areas there
 * right after VSYNC (no flashes of half-drawn areas, no tearing).
 */
namespace lvgl_port {

struct Stats {
  uint32_t frames = 0;        // refreshes presented to the panel
  uint32_t last_push_us = 0;  // copy + cache write-back time of the last refresh
  uint32_t touch_presses = 0;
};

bool init();      // lcd + touch + LVGL drivers; backlight must be initialised separately
void handle();    // call from loop(): runs lv_timer_handler()
const Stats &stats();
bool touch_ok();  // touch controller ACKed at init
const uint16_t *framebuffer();  // full 480x480 frame, little-endian RGB565 (read from the UI task only)

// Screen off: the next touch only wakes the screen (it never reaches widgets: the press is swallowed
// until the finger lifts). on_wake runs in the UI task.
void set_screen_off(bool off);
bool screen_off();
void set_wake_callback(void (*on_wake)());
bool touch_pressed();  // last polled touch state

}  // namespace lvgl_port
