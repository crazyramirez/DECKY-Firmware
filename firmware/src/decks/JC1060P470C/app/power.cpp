#include "power.h"

#include <Arduino.h>

#include "app/state_store.h"
#include "board/backlight.h"
#include "board/lvgl_port.h"
#include "board_pins.h"
#include "ui/ui.h"

namespace power {
namespace {

const char *TAG = "power";
constexpr uint32_t kDebounceMs = 40;

uint32_t s_presses = 0;
bool s_btn_low = false;        // debounced level
bool s_btn_raw = false;
uint32_t s_btn_raw_since = 0;

void on_touch_wake() {
  backlight::set(app::state().settings.brightness);
  ESP_LOGI(TAG, "screen on (touch)");
}

}  // namespace

void init() {
  pinMode(PCD_PIN_BOOT_BTN, INPUT_PULLUP);
  s_btn_raw = s_btn_low = digitalRead(PCD_PIN_BOOT_BTN) == LOW;  // held at boot: ignored until released
  s_btn_raw_since = millis();
  lvgl_port::set_wake_callback(on_touch_wake);
}

void poll(uint32_t now) {
  // BOOT button: a press toggles the screen (a physical way back from a dark screen).
  const bool raw = digitalRead(PCD_PIN_BOOT_BTN) == LOW;
  if (raw != s_btn_raw) {
    s_btn_raw = raw;
    s_btn_raw_since = now;
  }
  if (s_btn_raw != s_btn_low && now - s_btn_raw_since >= kDebounceMs) {
    s_btn_low = s_btn_raw;
    if (s_btn_low) {
      s_presses++;
      ESP_LOGI(TAG, "BOOT button");
      if (ui::screensaver::active())
        ui::screensaver::stop();
      else if (lvgl_port::screen_off())
        screen_on();
      else
        screen_off();
    }
  }
}

void screen_off() {
  if (lvgl_port::screen_off()) return;
  ui::screensaver::stop();
  // Backlight only: the panel keeps its frame, touch keeps scanning so a tap wakes it.
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
uint32_t boot_presses() { return s_presses; }

}  // namespace power
