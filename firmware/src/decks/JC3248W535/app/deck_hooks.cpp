// Guition JC3248W535: console status lines and commands of this deck (deep sleep, setup wizard).
#include "app/deck_hooks.h"

#include <stdlib.h>
#include <string.h>

#include "app/power.h"
#include "board/console_io.h"
#include "board/lvgl_port.h"
#include "ui/ui_internal.h"

namespace deck {

void console_status() {
  con::printf("screen %s | touch presses=%u int_low_samples=%u\n", power::is_screen_off() ? "off" : "on",
              static_cast<unsigned>(lvgl_port::stats().touch_presses), static_cast<unsigned>(power::touch_int_lows()));
}

const char *console_help() {
  return "          sleep   (deep sleep; wake: touch or BOOT)\n"
         "          setup [1-5|off]   (setup wizard at that step; nothing is erased)";
}

bool console_command(const char *cmd, const char *rest) {
  if (!strcmp(cmd, "sleep")) {
    con::println("OK deep sleep (wake: touch or BOOT)");
    power::deep_sleep();
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
