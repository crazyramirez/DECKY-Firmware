#pragma once
#include <stdint.h>

/*
 * LVGL <-> board glue. Threading rule: every lv_* call happens in the Arduino loop task.
 * Other tasks (network, timers) must post to the app event queue instead of touching widgets.
 *
 * Render strategy: LVGL "partial" mode into two internal-RAM band buffers (PCD_LVGL_BUF_LINES
 * lines each). Each finished band goes to the JD9165 DPI frame buffer through the DMA2D engine while
 * LVGL renders the next band; LVGL only redraws dirty areas.
 */
namespace lvgl_port {

struct Stats {
  uint32_t frames = 0;        // refreshes pushed to the panel
  uint32_t last_push_us = 0;  // DMA2D copy time of the last band
  uint32_t touch_presses = 0;
  uint32_t flush_timeouts = 0;
  uint32_t last_refr_ms = 0;  // render + flush time of the last refresh (LVGL monitor_cb)
  uint32_t last_refr_px = 0;  // pixels redrawn by it
  uint32_t max_refr_ms = 0;   // slowest refresh since the last reset_max_refr()
};
void reset_max_refr();

bool init();      // lcd + touch + LVGL drivers; backlight must be initialised separately
void handle();    // call from loop(): runs lv_timer_handler()
const Stats &stats();
bool touch_ok();  // touch controller ACKed at init
// The panel frame (PCD_LCD_H_RES x PCD_LCD_V_RES, little-endian RGB565), cache-synced for reading.
// UI task only (no band is being copied in while the UI task runs this).
const uint16_t *framebuffer();

// Screen off: bands are not copied and the next touch only wakes the screen (it never reaches
// widgets: the press is swallowed until the finger lifts). on_wake runs in the UI task.
void set_screen_off(bool off);
bool screen_off();
void set_wake_callback(void (*on_wake)());
bool touch_pressed();  // last polled touch state

}  // namespace lvgl_port
