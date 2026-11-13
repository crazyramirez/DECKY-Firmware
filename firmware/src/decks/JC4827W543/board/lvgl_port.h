#pragma once
#include <stdint.h>

/*
 * LVGL <-> board glue. Threading rule: every lv_* call happens in the Arduino loop task.
 * Other tasks (network, timers) must post to the app event queue instead of touching widgets.
 *
 * Render strategy: LVGL "direct mode" into one full-screen PSRAM buffer. LVGL only redraws
 * dirty areas, and on the last area of a refresh the frame is pushed to the NV3041A panel (whole
 * frame by default, like the vendor demos; PCD_LCD_PARTIAL_ROWS=1 pushes the dirty row band).
 */
namespace lvgl_port {

struct Stats {
  uint32_t frames = 0;        // frames pushed to the panel
  uint32_t last_push_us = 0;  // QSPI transfer time of the last frame
  uint32_t touch_presses = 0;
};

bool init();      // lcd + touch + LVGL drivers; backlight must be initialised separately
void handle();    // call from loop(): runs lv_timer_handler()
const Stats &stats();
bool touch_ok();  // touch controller ACKed at init
const uint16_t *framebuffer();  // full 480x272 frame, big-endian RGB565 (read from the UI task only)

// Screen off: frames are not pushed and the next touch only wakes the screen (it never reaches
// widgets: the press is swallowed until the finger lifts). on_wake runs in the UI task.
void set_screen_off(bool off);
bool screen_off();
void set_wake_callback(void (*on_wake)());
bool touch_pressed();  // last polled touch state

}  // namespace lvgl_port
