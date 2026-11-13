// Guition JC4827W543: console status lines and commands of this deck (deep sleep, sleep button).
#include "app/deck_hooks.h"

#include <string.h>

#include "app/power.h"
#include "app/state_store.h"
#include "board/console_io.h"
#include "board/lvgl_port.h"
#include "board/touch_gt911.h"
#include "board_pins.h"

namespace deck {

void console_status() {
  const auto &st = app::state();
  uint16_t tx = 0, ty = 0;
  touch::config_resolution(tx, ty);
  con::printf("touch GT911 addr=0x%02X configured %ux%u reads=%u i2c_err=%u rejected=%u | frame push %u us\n",
              touch::address(), tx, ty, static_cast<unsigned>(touch::stats().reads),
              static_cast<unsigned>(touch::stats().i2c_errors), static_cast<unsigned>(touch::stats().rejected),
              static_cast<unsigned>(lvgl_port::stats().last_push_us));
  con::printf("screen %s | touch presses=%u int_low_samples=%u\n", power::is_screen_off() ? "off" : "on",
              static_cast<unsigned>(lvgl_port::stats().touch_presses), static_cast<unsigned>(power::touch_int_lows()));
  int ia = 0, ib = 0;
  uint32_t taps = 0;
  power::button_info(ia, ib, taps);
  con::printf("sleep button GPIO%d idle=%d GPIO%d idle=%d taps=%u action=%s | wake reason '%s'\n", PCD_PIN_SLEEP_BTN_A,
              ia, PCD_PIN_SLEEP_BTN_B, ib, static_cast<unsigned>(taps),
              st.settings.button_action == 1 ? "screen" : "deep sleep", power::wake_reason());
}

const char *console_help() { return "          sleep   (deep sleep; wake: sleep button, touch or BOOT)"; }

bool console_command(const char *cmd, const char *) {
  if (strcmp(cmd, "sleep") != 0) return false;
  con::println("OK deep sleep (wake: sleep button, touch or BOOT)");
  power::deep_sleep();
}

}  // namespace deck
