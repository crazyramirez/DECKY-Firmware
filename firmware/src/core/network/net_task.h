#pragma once
// Network task (core 0): Wi-Fi station + WebSocket client to the PC agent + mDNS discovery.
// Never touches LVGL: it only posts app::Event (Telemetry / LinkChanged / AgentCaps / WifiScanDone /
// DiscoveryDone / PairDone) to the bus.
//
// Link states it reports:  NoWifi (no credentials / Wi-Fi down) -> Discovering (Wi-Fi up, no PC
// paired) -> Connecting -> Pairing (connected but not paired) / Unauthorized (token rejected) ->
// Online (authenticated + capabilities). Freshness (STALE/OFFLINE) is decided by the app.

#include <stdint.h>

#include "telemetry/models.h"

namespace net {

struct Status {
  bool wifi_connected = false;
  bool ws_connected = false;
  bool authenticated = false;
  bool paired = false;  // a device token is stored
  int8_t rssi = 0;
  char ip[16] = "";
  char agent[72] = "";       // host:port
  char agent_name[40] = "";  // paired PC name
  uint32_t reconnects = 0;
  uint32_t rx_frames = 0;
  uint32_t rx_rejected = 0;
  uint32_t backoff_ms = 0;
  uint8_t last_disconnect_reason = 0;  // wifi_err_reason_t of the last STA disconnect (0 = none)
  bool scanning = false;
  bool discovering = false;
  bool pairing = false;
};

// Wi-Fi scan result (UI Wi-Fi manager). Hidden SSIDs are dropped; duplicates keep the strongest AP.
struct ScanEntry {
  char ssid[33] = "";
  int8_t rssi = -127;
  bool secure = true;
};
constexpr int kMaxScan = 20;

// PC agent found over mDNS (_pcdeck._tcp).
struct PcEntry {
  char name[40] = "";
  char host[16] = "";  // IPv4
  uint16_t port = 8765;
  char agent_id[24] = "";
};
constexpr int kMaxPcs = 8;
constexpr int kDiscoveryNoWifi = -2;
constexpr int kDiscoveryFailed = -1;

enum class Failure : uint8_t { None, WrongPassword, NotFound, Other };

bool start();                // creates the task (idle until enabled)
void set_enabled(bool on);   // false = radio off, nothing leaves the device (demo mode)
void reconfigure();          // credentials changed in NVS: reconnect with the new values
Status status();             // thread-safe copy, for diagnostics
void request_scan();         // async; posts app::EventType::WifiScanDone when finished
int scan_results(ScanEntry *out, int max);  // copy of the last scan, sorted by RSSI (-1 = scan failed)
Failure classify(uint8_t reason);           // map a disconnect reason for the UI

void request_discovery();                       // async mDNS browse; posts DiscoveryDone
int discovery_results(PcEntry *out, int max);   // >=0 count, kDiscoveryNoWifi, kDiscoveryFailed
void start_pairing(const PcEntry &pc, const char *code);  // async; posts PairDone
void forget_pc();                               // drop token + agent address (needs re-pairing)

// Weather (Open-Meteo): through the PC app when it is connected (agent >= 0.5), straight from the
// internet otherwise (web_source). The forecast for the chosen place is asked for on every connection
// and then every 10 min; results arrive as app::EventType::WeatherData.
void set_weather_place(bool set, float lat, float lon);  // also forces a fresh request
void request_weather();                                  // force a fresh request
constexpr int kPlacesSearching = -2;
constexpr int kPlacesOffline = -3;  // no Wi-Fi (without a PC the deck searches Open-Meteo itself)
constexpr int kPlacesFailed = -1;   // the geocoding service could not be reached
void search_places(const char *query, const char *lang);  // async; posts PlacesDone
int place_results(telemetry::Place *out, int max);        // count >= 0 or kPlaces*

// Crypto (Binance / CoinGecko / alternative.me): through the PC app when connected (agent >= 0.6),
// straight from the internet otherwise. `coins` are
// COIN_CATALOG indices. While active, prices are asked for every period_ms (>= 10 s); a change of
// coins / currency (or becoming active) asks at once. Results: app::EventType::CryptoData /
// CryptoChart, data in crypto_store.
void set_crypto(const int8_t *coins, int n, const char *cur, uint32_t period_ms, bool active);
void request_crypto();                                                // fresh prices as soon as allowed
void set_agent_paused(bool paused);  // developer aid (USB console "nopc"): ignore the paired PC, not saved
void request_crypto_chart(int coin, uint8_t range, const char *cur);  // latest request wins

}  // namespace net
