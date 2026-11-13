#include "power.h"

#include <Arduino.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "board/backlight.h"
#include "board/lvgl_port.h"
#include "ui/ui.h"

namespace power {
namespace {

const char *TAG = "power";

void on_touch_wake() {
  backlight::set(app::screen_brightness());
  ESP_LOGI(TAG, "screen on (touch)");
}

}  // namespace

void init() { lvgl_port::set_wake_callback(on_touch_wake); }

void poll(uint32_t) {}

void screen_off() {
  if (lvgl_port::screen_off()) return;
  ui::screensaver::stop();
  // Backlight only: the panel keeps scanning its frame, touch keeps scanning so a tap wakes it.
  backlight::set(0);
  lvgl_port::set_screen_off(true);
  ESP_LOGI(TAG, "screen off");
}

void screen_on() {
  if (!lvgl_port::screen_off()) return;
  lvgl_port::set_screen_off(false);
  on_touch_wake();
}

bool is_screen_off() { return lvgl_port::screen_off(); }

}  // namespace power
