#pragma once
// Bounded FreeRTOS queue from producer tasks (demo source now, network client later) to the
// UI task. Only the UI task (Arduino loop) drains it and touches LVGL.

#include <stdint.h>

#include "telemetry/models.h"

namespace app {

enum class LinkState : uint8_t {
  Booting,
  NoWifi,
  Discovering,
  Pairing,
  Connecting,
  Online,
  Stale,
  Offline,
  Unauthorized,
  Error,
};

enum class EventType : uint8_t {
  Telemetry,      // payload.snapshot
  LinkChanged,    // payload.link
  CommandResult,  // payload.cmd
  AgentCaps,      // payload.caps (telemetry.capabilities from the agent)
  WifiScanDone,   // no payload: read net::scan_results()
  DiscoveryDone,  // no payload: read net::discovery_results()
  PairDone,       // payload.pair
  ArtReady,       // payload.art: the receiver OWNS art.data (heap_caps_free it)
  WeatherData,    // payload.weather (ok == false: error set)
  PlacesDone,     // no payload: read net::place_results()
  CryptoData,     // no payload: read crypto::get_market() / crypto::market_error()
  CryptoChart,    // no payload: read crypto::get_chart()
};

struct ArtFrame {
  uint32_t art_id = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t *data = nullptr;  // RGB565 big-endian as sent by the PC app (LV_COLOR_16_SWAP=0 decks swap it), PSRAM
};

struct PairOutcome {
  bool ok = false;
  char error[24] = "";  // "invalid_code", "timeout", "unreachable", ...
  char agent_name[40] = "";
};

enum class CmdStatus : uint8_t { Ack, Nack, Timeout };

struct AgentCaps {
  bool volume = false, media = false, lock = false, power = false, seek = false;
  bool auth_required = false;
  char provider[32] = "";
  char first_note[96] = "";
  uint8_t note_count = 0;
};

struct CommandResult {
  uint32_t request_id = 0;
  CmdStatus status = CmdStatus::Nack;
  int32_t applied_value = -1;
  char action[24] = "";
  char error[40] = "";
  bool simulated = false;
};

struct Event {
  EventType type;
  union {
    telemetry::Snapshot snapshot;
    LinkState link;
    CommandResult cmd;
    AgentCaps caps;
    PairOutcome pair;
    ArtFrame art;
    telemetry::Weather weather;
  };
  Event() : type(EventType::LinkChanged), link(LinkState::Booting) {}
};

namespace bus {
bool init(int depth = 4);
bool post(const Event &e, uint32_t timeout_ms = 0);  // from any task; drops if full
bool poll(Event &out);                                // UI task only
uint32_t dropped();
}  // namespace bus

}  // namespace app
