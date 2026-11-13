#include "battery.h"

#include <Arduino.h>

#include "board_pins.h"
#include "driver/usb_serial_jtag.h"

namespace battery {
namespace {

constexpr uint32_t kPeriodMs = 1000;
State s_state;
uint32_t s_next = 0;

#if PCD_PIN_BAT_ADC >= 0
// Divider ratio between the battery node and the ADC pin (only when PCD_PIN_BAT_ADC >= 0).
#ifndef PCD_BAT_DIVIDER
#define PCD_BAT_DIVIDER 2.0f
#endif

constexpr int kSamples = 16;
constexpr uint16_t kMinCellMv = 2900;  // below: nothing measurable
// The node follows the cell whenever a battery is attached (also while it charges from USB), so the
// voltage alone cannot tell USB from battery. USB = a USB host is talking to us (SOF frames, IDF
// timer-based check) OR the node is above any cell voltage (~4.9 V: USB without a battery).
// Hysteresis + a few consecutive samples avoid flicker. A charge-only wall adapter (no data) with a
// battery attached is indistinguishable from battery power on this board.
constexpr uint16_t kUsbEnterMv = 4700;
constexpr uint16_t kUsbExitMv = 4550;
constexpr int kConfirmSamples = 3;  // seconds a new source must persist before it is shown

float s_filtered = 0;
Source s_candidate = Source::Unknown;
int s_candidate_n = 0;

// Typical single-cell LiPo curve under light load (mV -> %).
int pct_from_mv(uint16_t mv) {
  static const uint16_t kMv[] = {3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 4000, 4100, 4180};
  static const uint8_t kPct[] = {0, 5, 10, 22, 32, 45, 55, 65, 80, 92, 100};
  constexpr int n = sizeof(kMv) / sizeof(kMv[0]);
  if (mv <= kMv[0]) return 0;
  if (mv >= kMv[n - 1]) return 100;
  for (int i = 1; i < n; i++) {
    if (mv <= kMv[i]) return kPct[i - 1] + (kPct[i] - kPct[i - 1]) * (mv - kMv[i - 1]) / (kMv[i] - kMv[i - 1]);
  }
  return 100;
}

#endif

}  // namespace

void init() {
#if PCD_PIN_BAT_ADC >= 0
  analogSetPinAttenuation(PCD_PIN_BAT_ADC, ADC_11db);  // 0..~3.1 V at the pin
  pinMode(PCD_PIN_BAT_ADC, INPUT);
#endif
}

void poll(uint32_t now_ms) {
  if (static_cast<int32_t>(now_ms - s_next) < 0) return;
  s_next = now_ms + kPeriodMs;
#if PCD_PIN_BAT_ADC < 0
  // No battery-sense line on this board: only "a USB host is talking to us" is known.
  State nb;
  nb.source = usb_serial_jtag_is_connected() ? Source::Usb : Source::Unknown;
  s_state = nb;
  return;
#else
  uint32_t sum = 0;
  for (int i = 0; i < kSamples; i++) sum += analogReadMilliVolts(PCD_PIN_BAT_ADC);
  const float pin = static_cast<float>(sum) / kSamples;
  const float node = pin * PCD_BAT_DIVIDER;
  // Fast follow on big jumps (USB plugged/unplugged), smoothing otherwise.
  const bool jump = s_filtered <= 0 || node - s_filtered > 300 || s_filtered - node > 300;
  s_filtered = jump ? node : s_filtered * 0.85f + node * 0.15f;

  State st;
  st.pin_mv = static_cast<uint16_t>(pin);
  st.mv = static_cast<uint16_t>(s_filtered);
  const bool was_usb = s_state.source == Source::Usb;
  Source raw = Source::Unknown;
  if (usb_serial_jtag_is_connected() || st.mv >= (was_usb ? kUsbExitMv : kUsbEnterMv)) raw = Source::Usb;
  else if (st.mv >= kMinCellMv) raw = Source::Battery;
  // Debounce source changes (the first reading after boot is taken as is).
  if (s_state.source == Source::Unknown || raw == s_state.source) {
    s_candidate_n = 0;
    st.source = raw;
  } else {
    if (raw != s_candidate) {
      s_candidate = raw;
      s_candidate_n = 0;
    }
    st.source = ++s_candidate_n >= kConfirmSamples ? raw : s_state.source;
    if (st.source == raw) s_candidate_n = 0;
  }
  if (st.source == Source::Battery) {
    // Hysteresis: never let the shown percentage go up while discharging (load-dependent ripple).
    const int p = pct_from_mv(st.mv);
    st.pct = static_cast<int8_t>(s_state.source == Source::Battery && s_state.pct >= 0 && p > s_state.pct &&
                                         p - s_state.pct < 5
                                     ? s_state.pct
                                     : p);
  }
  s_state = st;
#endif
}

State state() { return s_state; }

const char *source_name(Source s) {
  switch (s) {
    case Source::Usb: return "usb";
    case Source::Battery: return "battery";
    default: return "unknown";
  }
}

}  // namespace battery
