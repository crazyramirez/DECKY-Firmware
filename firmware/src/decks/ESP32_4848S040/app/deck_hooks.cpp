// Guition ESP32-4848S040: console status lines and commands of this deck (setup wizard).
#include "app/deck_hooks.h"

#include <stdlib.h>
#include <string.h>

#include "app/power.h"
#include "board/console_io.h"
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
}

const char *console_help() { return "          setup [1-5|off]   (setup wizard at that step; nothing is erased)"; }

bool console_command(const char *cmd, const char *rest) {
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
