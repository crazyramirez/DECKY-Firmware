#include "state_store.h"

#include <time.h>

namespace app {
namespace {
AppState s_state;
}

AppState &state() { return s_state; }

uint8_t ingest(const telemetry::Snapshot &s, uint32_t now_ms) {
  AppState &st = s_state;
  st.snap = s;
  st.has_snapshot = true;
  st.last_rx_ms = now_ms;
  st.rx_count++;

  st.cpu_load.push(s.cpu.load_pct);
  st.cpu_temp.push(s.cpu.temp_c);
  st.gpu_load.push(s.gpu.load_pct);
  st.gpu_temp.push(s.gpu.temp_c);

  uint8_t notify = 0;
  if (st.cpu_alert.update(s.cpu.temp_c, now_ms)) notify |= 0x01;
  if (st.gpu_alert.update(s.gpu.temp_c, now_ms)) notify |= 0x02;
  return st.settings.alerts_enabled ? notify : 0;
}

void clear_history() {
  AppState &st = s_state;
  st.cpu_load.clear();
  st.cpu_temp.clear();
  st.gpu_load.clear();
  st.gpu_temp.clear();
  st.cpu_alert.reset();
  st.gpu_alert.reset();
  st.has_snapshot = false;
}

void apply_alert_thresholds() {
  AppState &st = s_state;
  alerts::Thresholds cpu{st.settings.cpu_warn_c, st.settings.cpu_crit_c};
  alerts::Thresholds gpu{st.settings.gpu_warn_c, st.settings.gpu_crit_c};
  st.cpu_alert.set_thresholds(cpu);
  st.gpu_alert.set_thresholds(gpu);
}

bool controls_available() { return s_state.link == LinkState::Online; }

bool data_online() {
  const AppState &st = s_state;
  return !st.settings.demo_mode && st.link != LinkState::NoWifi && st.link != LinkState::Booting;
}

uint8_t hidden_tabs() {
  uint8_t h = s_state.settings.tab_hidden;
  if (!s_state.pc_present) {
    h |= static_cast<uint8_t>((1u << static_cast<uint8_t>(storage::Tab::Monitor)) |
                              (1u << static_cast<uint8_t>(storage::Tab::Thermals)) |
                              (1u << static_cast<uint8_t>(storage::Tab::Player)));
  }
  return h;
}

bool local_time(struct tm &out) {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;  // not set yet (no PC clock, no SNTP)
  const time_t local = now + static_cast<time_t>(s_state.settings.utc_offset_min) * 60;
  gmtime_r(&local, &out);
  return true;
}

}  // namespace app
