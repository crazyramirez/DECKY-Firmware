#pragma once
// Deck power source. Without a battery-sense line (PCD_PIN_BAT_ADC = -1: JC1060P470C, JC4827W543)
// only "USB host present" is reported (Source::Usb, else Unknown). With a 2:1 divider on an ADC pin
// (PCD_PIN_BAT_ADC >= 0: the JC3248W535's BAT connector on GPIO5) the full logic below applies:
//  - USB: detected from USB host activity (or ~4.9 V at the node when no cell is attached);
//  - battery: the node follows the cell voltage (3.0..4.2 V): percentage from a LiPo curve.
#include <stdint.h>

#include "board_pins.h"

namespace battery {

enum class Source : uint8_t { Unknown, Usb, Battery };

struct State {
  Source source = Source::Unknown;
  uint16_t mv = 0;      // node voltage (filtered); the cell voltage when source == Battery
  int8_t pct = -1;      // 0..100 on battery, -1 otherwise
  uint16_t pin_mv = 0;  // raw ADC pin voltage (diagnostics / calibration)
};

// This deck can tell battery from USB power (a battery-sense line): only then is there a battery
// level to show anywhere (top bar, screensaver, its setting).
constexpr bool sensed() { return PCD_PIN_BAT_ADC >= 0; }

void init();
void poll(uint32_t now_ms);  // UI task, cheap; samples every second
State state();
// Developer aid (USB console "batlog"): the last readings, one every 5 s (about 10 minutes), kept in
// RAM while the deck runs: unplug, wait, plug back in and read what the deck saw on its battery.
struct LogEntry {
  uint32_t t_s = 0;     // uptime, seconds
  uint16_t node_mv = 0; // filtered node voltage
  uint16_t raw_mv = 0;  // this second's node reading (unfiltered)
  uint8_t source = 0;   // Source shown
  bool usb_host = false;
};
int log_entries(LogEntry *out, int max);  // oldest first
const char *source_name(Source s);

}  // namespace battery
