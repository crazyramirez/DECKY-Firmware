// Guition JC1060P470C: console status lines and developer commands of this deck.
#include "app/deck_hooks.h"

#include <stdio.h>
#include <string.h>

#include "app/power.h"
#include "board/console_io.h"
#include "board/lvgl_port.h"
#include "board/touch_gt911.h"
#include "ui/ui_internal.h"

namespace deck {

void console_status() {
  uint16_t tx = 0, ty = 0;
  touch::config_resolution(tx, ty);
  con::printf("touch GT911 addr=0x%02X configured %ux%u reads=%u i2c_err=%u rejected=%u\n", touch::address(), tx, ty,
              static_cast<unsigned>(touch::stats().reads), static_cast<unsigned>(touch::stats().i2c_errors),
              static_cast<unsigned>(touch::stats().rejected));
  const auto &ps = lvgl_port::stats();
  con::printf("display frames=%u band copy %u us flush timeouts=%u | screen %s | touch presses=%u | BOOT presses=%u\n",
              static_cast<unsigned>(ps.frames), static_cast<unsigned>(ps.last_push_us),
              static_cast<unsigned>(ps.flush_timeouts), power::is_screen_off() ? "off" : "on",
              static_cast<unsigned>(ps.touch_presses), static_cast<unsigned>(power::boot_presses()));
  con::printf("render last %u ms (%u px) max %u ms since last status\n", static_cast<unsigned>(ps.last_refr_ms),
              static_cast<unsigned>(ps.last_refr_px), static_cast<unsigned>(ps.max_refr_ms));
  lvgl_port::reset_max_refr();
}

const char *console_help() { return "          dbg <grad|art|clip|marquee> <0|1>   (rendering experiments)"; }

bool console_command(const char *cmd, const char *rest) {
  if (strcmp(cmd, "dbg") != 0) return false;
  char what[16];
  int on = 1;
  if (sscanf(rest, "%15s %d", what, &on) == 2) {
    ui::controls::dev_tweak(what, on);
    con::println("OK");
  } else {
    con::println("ERR usage: dbg <grad|art|clip|marquee> <0|1>");
  }
  return true;
}

}  // namespace deck
