// Guition ESP32-4848S040: console status lines and commands of this deck (setup wizard, display bench).
#include "app/deck_hooks.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/power.h"
#include "board/console_io.h"
#include "board/lcd_st7701.h"
#include "board/lvgl_port.h"
#include "board/touch_gt911.h"
#include "ui/ui_internal.h"

namespace deck {

void console_status() {
  const touch::Stats &t = touch::stats();
  con::printf("touch GT911 addr=0x%02X reads=%u i2c_err=%u rejected=%u | screen %s | touch presses=%u\n",
              touch::address(), static_cast<unsigned>(t.reads), static_cast<unsigned>(t.i2c_errors),
              static_cast<unsigned>(t.rejected), power::is_screen_off() ? "off" : "on",
              static_cast<unsigned>(lvgl_port::stats().touch_presses));
  const auto &ps = lvgl_port::stats();
  con::printf("render last %u ms (%u px; wait %u us, copy %u us) max %u ms since last status | frames=%u\n",
              static_cast<unsigned>(ps.last_refr_ms), static_cast<unsigned>(ps.last_refr_px),
              static_cast<unsigned>(ps.last_wait_us), static_cast<unsigned>(ps.last_push_us),
              static_cast<unsigned>(ps.max_refr_ms), static_cast<unsigned>(ps.frames));
  con::printf("copy %u B in %d areas\n", static_cast<unsigned>(lcd::last_bytes()), lcd::last_rects());
  lvgl_port::reset_max_refr();
}

const char *console_help() {
  return "          setup [1-5|off]   (setup wizard at that step; nothing is erased)\n"
         "          bench [n]         (redraw the whole screen n times: render / wait / copy)";
}

namespace {

// Whole-screen redraws of what is shown, timed: what LVGL takes to draw it, how long the copy to the
// panel waited for the scan, and the copy itself.
void bench(int n) {
  uint32_t total = 0, wait = 0, copy = 0, worst = 0;
  for (int i = 0; i < n; i++) {
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());  // the screen is skipped under a full-screen sheet
    const uint32_t t0 = micros();
    lv_refr_now(nullptr);
    const uint32_t dt = micros() - t0;
    total += dt;
    if (dt > worst) worst = dt;
    wait += lvgl_port::stats().last_wait_us;
    copy += lvgl_port::stats().last_push_us;
  }
  const uint32_t render = total > wait + copy ? total - wait - copy : 0;
  con::printf("OK bench %d full redraws: %u ms each (render %u, wait %u, copy %u; worst %u) = %u fps\n", n,
              static_cast<unsigned>(total / n / 1000), static_cast<unsigned>(render / n / 1000),
              static_cast<unsigned>(wait / n / 1000), static_cast<unsigned>(copy / n / 1000),
              static_cast<unsigned>(worst / 1000), static_cast<unsigned>(1000000ULL * n / (total ? total : 1)));
}

}  // namespace

bool console_command(const char *cmd, const char *rest) {
  if (!strcmp(cmd, "bench")) {
    const long n = *rest ? strtol(rest, nullptr, 10) : 10;
    if (n < 1 || n > 100) {
      con::println("ERR usage: bench [1-100]");
    } else {
      bench(static_cast<int>(n));
    }
    return true;
  }
  if (strcmp(cmd, "setup") != 0) return false;
  // setup [1-5] opens the wizard at that step; setup off closes it. Stored settings are untouched.
  if (!strcmp(rest, "off")) {
    ui::setup::close();
    con::println("OK");
  } else {
    const long step = *rest ? strtol(rest, nullptr, 10) : 1;
    if (step < 1 || step > 5) {
      con::println("ERR usage: setup [1-5|off]");
    } else {
      ui::setup::open(static_cast<int>(step - 1));
      con::println("OK");
    }
  }
  return true;
}

}  // namespace deck
