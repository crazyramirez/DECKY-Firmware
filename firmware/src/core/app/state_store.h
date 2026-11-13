#pragma once
// Single source of truth for the UI. Owned and mutated only by the UI task.

#include <stdint.h>
#include <time.h>

#include "alerts/thermal_alerts.h"
#include "app/event_bus.h"
#include "storage/settings_store.h"
#include "telemetry/history.h"
#include "telemetry/models.h"

namespace app {

constexpr uint32_t kStaleAfterMs = 4000;     // spec: 3–5 s
constexpr uint32_t kOfflineAfterMs = 12000;  // spec: 10–15 s

struct Capabilities {
  bool volume = false;
  bool media = false;
  bool lock = false;
  bool power = false;  // shutdown/restart: off unless the agent explicitly enables it
  bool seek = false;   // media.seek (agent >= 0.4)
};

struct AppState {
  storage::Settings settings;
  LinkState link = LinkState::Booting;
  LinkState net_link = LinkState::NoWifi;  // as reported by the network task (non-demo mode)
  Capabilities caps;
  char agent_provider[32] = "";
  char agent_first_note[96] = "";
  uint8_t agent_note_count = 0;
  bool has_snapshot = false;
  telemetry::Snapshot snap;
  uint32_t last_rx_ms = 0;
  uint32_t rx_count = 0;

  telemetry::History60 cpu_load, cpu_temp, gpu_load, gpu_temp;

  // Today tab. weather.ok: a forecast for settings.place_*; weather_error: last failure ("" = none).
  telemetry::Weather weather;
  uint32_t weather_rx_ms = 0;
  char weather_error[24] = "";

  // A PC app is connected (or a paired one may still connect, right after boot). Only then are the
  // PC pages (Monitor, Thermals, Player) in the tab bar; weather and crypto don't need a PC.
  bool pc_present = true;

  alerts::ThermalAlert cpu_alert{alerts::Thresholds{85.0f, 95.0f}};
  alerts::ThermalAlert gpu_alert{alerts::Thresholds{80.0f, 90.0f}};
};

AppState &state();

// Ingest a snapshot (updates histories + alerts). Returns bitmask of alerts to notify:
// bit0 = CPU, bit1 = GPU.
uint8_t ingest(const telemetry::Snapshot &s, uint32_t now_ms);
void clear_history();
void apply_alert_thresholds();

// True when sensitive controls may be used (fresh data from an agent or demo source).
bool controls_available();

// Weather and crypto can be fetched: Wi-Fi is up (through the PC app or straight from the internet).
bool data_online();

// Tabs left out of the bar: the user's choice (Settings > Tabs) plus the PC pages while no PC is there.
uint8_t hidden_tabs();

// Local wall-clock time (PC clock / SNTP + the PC's UTC offset). False until the clock is set.
bool local_time(struct tm &out);

}  // namespace app
