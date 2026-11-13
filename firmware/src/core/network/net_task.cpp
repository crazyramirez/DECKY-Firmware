#include "network/net_task.h"

#include "deck_config.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/event_bus.h"
#include "commands/command_client.h"
#include "crypto/coin_catalog.h"
#include "crypto/crypto_store.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/tcpip.h"
#include "network/protocol_parser.h"
#include "network/web_source.h"
#include "storage/net_store.h"

namespace net {
namespace {

const char *TAG = "net";

constexpr uint32_t kWifiRetryMs = 15000;
constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kBackoffMinMs = 1000;
constexpr uint32_t kBackoffMaxMs = 30000;
constexpr uint32_t kPairTimeoutMs = 15000;
constexpr uint32_t kMaxFailStreak = 3;

TaskHandle_t s_task = nullptr;
volatile bool s_enabled = false;
volatile bool s_reconfigure = true;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Status s_status;

WebSocketsClient s_ws;
storage::NetConfig s_cfg;  // contains the token: never log it
bool s_wifi_begun = false;
uint32_t s_wifi_begin_ms = 0;
// A fresh connection attempt (new network, power-on, a dropped link) often sees a failed
// association or two before it gets in -- the ESP32-C6 behind ESP-Hosted in particular -- and
// stopping the radio reports our own ASSOC_LEAVE. The UI shows "Connecting to <network>…" and
// gets a reason (wrong password, not found...) only once the attempt has kept failing.
constexpr uint32_t kFailShowAfterMs = 12000;
constexpr uint8_t kReasonAssocLeave = 8;  // WIFI_REASON_ASSOC_LEAVE: we disconnected ourselves
volatile uint32_t s_attempt_start_ms = 0;  // when this attempt began (0 = connected / none)
volatile uint8_t s_pending_reason = 0;     // last real disconnect reason of this attempt
bool s_ws_started = false;
bool s_ws_open = false;
bool s_caps_received = false;
bool s_authenticated = false;
bool s_auth_rejected = false;  // token refused: stop reconnecting until the user re-pairs
uint32_t s_attempt = 0;
uint32_t s_fail_streak = 0;
bool s_force_wifi_reconnect = false;
uint32_t s_last_hb = 0;
// Album art recovery: the agent pushes each cover once per track. If the deck missed it (dropped
// frame, full event queue...), ask again ("media.art_request") while the snapshot names an artId
// the UI never got. Older agents answer with an "unknown type" error, harmless.
uint32_t s_art_have = 0;           // last art id handed to the UI
uint32_t s_art_missing_since = 0;  // millis() when the current mismatch was first seen (0 = none)
uint32_t s_art_req_at = 0;
constexpr uint32_t kArtGraceMs = 2500;
constexpr uint32_t kArtRetryMs = 5000;
void check_art(uint32_t wanted);

// Weather: place set by the UI (under s_mux); the forecast is asked for on each connection and then
// every 10 min (every 30 s until the first answer). The agent accepts one weather message a second.
bool s_place_set = false;
float s_place_lat = 0, s_place_lon = 0;
volatile bool s_weather_force = false;
bool s_weather_ok = false;     // got a forecast in this session
uint32_t s_weather_at = 0;     // last weather.request sent (0 = never in this session)
uint32_t s_weather_msg_at = 0; // last weather message of any kind
constexpr uint32_t kWeatherEveryMs = 10 * 60 * 1000;
constexpr uint32_t kWeatherRetryMs = 30 * 1000;
constexpr uint32_t kWeatherGapMs = 1100;
// City search (UI -> net under s_mux; results back the same way).
bool s_search_requested = false;
char s_search_q[64] = "";
char s_search_lang[4] = "es";
uint32_t s_search_at = 0;
int s_place_state = 0;  // count, or kPlaces*
telemetry::Place s_places[telemetry::kMaxPlaces];
constexpr uint32_t kSearchTimeoutMs = 12000;
// Crypto (agent >= 0.6): watch list set by the UI (under s_mux). Prices are asked for every
// `period` while active (every 10 s until the first answer of the session); chart requests are
// one-shot (latest wins) and time out after 10 s.
int8_t s_cr_idx[crypto::kMaxCoins];
uint8_t s_cr_n = 0;
char s_cr_cur[4] = "EUR";
uint32_t s_cr_period = 30000;
bool s_cr_active = false;
volatile bool s_cr_force = false;
bool s_cr_answered = false;
bool s_cr_old_agent = false;
uint32_t s_cr_at = 0;
bool s_ch_pending = false;  // under s_mux
int8_t s_ch_coin = 0;
uint8_t s_ch_range = 0;
char s_ch_cur[4] = "EUR";
bool s_ch_waiting = false;  // net task only
char s_ch_wait_pair[16] = "";
uint8_t s_ch_wait_range = 0;
char s_ch_wait_cur[4] = "";
uint32_t s_ch_sent_at = 0;
crypto::Market *s_market_buf = nullptr;  // PSRAM parse targets
crypto::Chart *s_chart_buf = nullptr;
char s_cr_tx[1024];
constexpr uint32_t kCryptoMinGapMs = 2100;  // the PC app drops crypto.request faster than 1 / 2 s
constexpr uint32_t kCryptoRetryMs = 10000;
constexpr uint32_t kChartGapMs = 350;
constexpr uint32_t kChartTimeoutMs = 10000;
// No PC app connected: weather, city search and crypto come straight from the internet
// (network/web_source), with the same refresh periods. The PC app takes over when it connects.
bool s_agent_paused = false;  // USB console "nopc 1": behave as if no PC were paired (tests)
uint32_t s_wx_rx_at = 0;      // last forecast from either source (millis, 0 = none yet)
uint32_t s_web_wx_at = 0;     // last direct forecast attempt
bool s_web_cr_ok = false;     // the last direct price refresh worked
bool s_web_enrich = false;    // sparklines / market caps / global / fear & greed may be due
bool s_web_dirty = false;     // enrich() changed the cache since the market was last posted
uint32_t s_web_post_at = 0;
constexpr uint32_t kWebWeatherRetryMs = 60 * 1000;
void fail_crypto(const char *error);
bool s_sntp_started = false;
bool s_dns_settled = false;  // settle_dns() ran on this connection

// Arduino's NetworkManager::hostByName() (every HTTPS request resolves through it) clears lwIP's DNS
// cache the first time it runs after the IP state changed, without taking the lwIP core lock. If
// SNTP is waiting on its own DNS query right then, SNTP's callback runs in our task, asks the next
// server, and lwIP asserts "Required to lock TCPIP core functionality" (a panic: seen a few seconds
// after boot, a weather request meeting SNTP's first lookup). So on every new connection one name is
// resolved here first, with SNTP stopped: later lookups have no state change left to clear for.
void settle_dns() {
  LOCK_TCPIP_CORE();
  const bool sntp_on = esp_sntp_enabled();
  if (sntp_on) esp_sntp_stop();
  UNLOCK_TCPIP_CORE();
  IPAddress addr;
  WiFi.hostByName("pool.ntp.org", addr);
  if (sntp_on) configTime(0, 0, "pool.ntp.org", "time.google.com");  // restarts it, under the lock
}
app::LinkState s_reported = app::LinkState::Booting;
char s_client_id[24] = "";
char s_device_name[24] = "";
bool s_mdns_started = false;

// Wi-Fi scan
volatile bool s_scan_requested = false;
bool s_scanning = false;
uint32_t s_scan_start = 0;
#if PCD_WIFI_ESP_HOSTED
uint8_t s_scan_tries = 0;      // failed scan starts in a row (the C6 may still be starting its STA)
uint32_t s_scan_retry_at = 0;
#endif
ScanEntry s_scan[kMaxScan];
int s_scan_count = 0;  // -1 = failed

// mDNS discovery
volatile bool s_discover_requested = false;
PcEntry s_pcs[kMaxPcs];
int s_pc_count = 0;

// Pairing (request copied in by the UI task under s_mux)
volatile bool s_pair_requested = false;
PcEntry s_pair_target;
char s_pair_code[7] = "";
bool s_pairing = false;
uint32_t s_pair_deadline = 0;

proto::Message s_msg;  // large (snapshot inside): static, used only by this task

void stop_ws();

template <typename F>
void with_status(F f) {
  portENTER_CRITICAL(&s_mux);
  f(s_status);
  portEXIT_CRITICAL(&s_mux);
}

void post_link(app::LinkState l) {
  if (l == s_reported) return;
  s_reported = l;
  app::Event e;
  e.type = app::EventType::LinkChanged;
  e.link = l;
  app::bus::post(e, 20);
}

void post_simple(app::EventType t) {
  app::Event e;
  e.type = t;
  app::bus::post(e, 20);
}

void post_pair_done(bool ok, const char *error, const char *agent_name) {
  app::Event e;
  e.type = app::EventType::PairDone;
  e.pair = app::PairOutcome{};
  e.pair.ok = ok;
  strlcpy(e.pair.error, error ? error : "", sizeof(e.pair.error));
  strlcpy(e.pair.agent_name, agent_name ? agent_name : "", sizeof(e.pair.agent_name));
  app::bus::post(e, 50);
}

void finish_pairing(bool ok, const char *error, const char *agent_name) {
  if (!s_pairing) return;
  s_pairing = false;
  with_status([](Status &s) { s.pairing = false; });
  ESP_LOGI(TAG, "pairing %s %s", ok ? "succeeded" : "failed:", ok ? "" : error);
  if (!ok) {
    // Back to the previous (persisted) PC, if any: never keep the new address with an old token.
    stop_ws();
    storage::load_net(s_cfg);
  }
  post_pair_done(ok, error, agent_name);
}

uint32_t next_backoff() {
  // 1 s, 2 s, 4 s ... capped at 30 s, +/-20 % jitter so several decks don't reconnect in lockstep.
  uint32_t base = kBackoffMinMs << (s_attempt < 5 ? s_attempt : 5);
  if (base > kBackoffMaxMs) base = kBackoffMaxMs;
  const int32_t jitter = static_cast<int32_t>(esp_random() % (base / 5 * 2 + 1)) - static_cast<int32_t>(base / 5);
  s_attempt++;
  return static_cast<uint32_t>(static_cast<int32_t>(base) + jitter);
}

void publish_cfg_status() {
  char agent[sizeof(Status::agent)];
  snprintf(agent, sizeof(agent), "%s:%u", s_cfg.host, s_cfg.port);
  char name[sizeof(Status::agent_name)];
  strlcpy(name, s_cfg.agent_name, sizeof(name));
  const bool paired = s_cfg.paired();
  with_status([&agent, &name, paired](Status &s) {
    memcpy(s.agent, agent, sizeof(s.agent));
    memcpy(s.agent_name, name, sizeof(s.agent_name));
    s.paired = paired;
  });
}

void send_json(const char *buf, size_t n) {
  if (n) s_ws.sendTXT(buf, n);
}

void on_pair_result(const proto::AuthInfo &a) {
  if (!a.ok || !a.token[0]) {
    finish_pairing(false, a.error[0] ? a.error : "rejected", "");
    return;
  }
  storage::NetConfig cfg;
  storage::load_net(cfg);
  strlcpy(cfg.host, s_pair_target.host, sizeof(cfg.host));
  cfg.port = s_pair_target.port;
  strlcpy(cfg.token, a.token, sizeof(cfg.token));
  strlcpy(cfg.agent_id, a.agent_id[0] ? a.agent_id : s_pair_target.agent_id, sizeof(cfg.agent_id));
  strlcpy(cfg.agent_name, a.agent_name[0] ? a.agent_name : s_pair_target.name, sizeof(cfg.agent_name));
  storage::save_net(cfg);
  s_cfg = cfg;
  memset(&cfg, 0, sizeof(cfg));
  s_auth_rejected = false;
  s_authenticated = true;  // the agent grants the session right after a successful pairing
  publish_cfg_status();
  finish_pairing(true, "", s_cfg.agent_name);
}

void handle_text(const uint8_t *payload, size_t len) {
  const proto::ParseResult r = proto::parse(reinterpret_cast<const char *>(payload), len, s_msg);
  if (r != proto::ParseResult::Ok) {
    with_status([](Status &s) { s.rx_rejected++; });
    ESP_LOGW(TAG, "rejected frame (%u B): %s", static_cast<unsigned>(len), proto::result_name(r));
    if (r == proto::ParseResult::UnsupportedVersion) s_ws.disconnect();
    return;
  }
  with_status([](Status &s) { s.rx_frames++; });
  app::Event e;
  switch (s_msg.type) {
    case proto::MsgType::Snapshot:
      e.type = app::EventType::Telemetry;
      e.snapshot = s_msg.snapshot;
      e.snapshot.rx_ms = millis();
      app::bus::post(e, 20);
      check_art(s_msg.snapshot.media_art_id);
      break;
    case proto::MsgType::Capabilities: {
      const auto &c = s_msg.caps;
      s_caps_received = true;
      s_authenticated = c.authenticated;
      with_status([&c](Status &s) { s.authenticated = c.authenticated; });
      if (!c.authenticated) {
        // Agent wants credentials. Auth was already sent on connect if we have a token.
        if (!s_cfg.paired() && !s_pairing) post_link(app::LinkState::Pairing);
        break;
      }
      e.type = app::EventType::AgentCaps;
      e.caps = app::AgentCaps{};
      e.caps.volume = c.volume;
      e.caps.media = c.media;
      e.caps.seek = c.seek;
      e.caps.lock = c.lock;
      e.caps.power = c.power;
      e.caps.auth_required = c.auth_required;
      strlcpy(e.caps.provider, c.provider, sizeof(e.caps.provider));
      strlcpy(e.caps.first_note, c.first_note, sizeof(e.caps.first_note));
      e.caps.note_count = c.note_count;
      app::bus::post(e, 20);
      ESP_LOGI(TAG, "capabilities: provider='%s' notes=%u", c.provider, c.note_count);
      post_link(app::LinkState::Online);
      break;
    }
    case proto::MsgType::CommandResult: {
      const auto &c = s_msg.cmd;
      if (c.request_id == 0) break;  // not one of ours (or from a previous boot)
      e.type = app::EventType::CommandResult;
      e.cmd = app::CommandResult{};
      e.cmd.request_id = c.request_id;
      e.cmd.status = c.ok ? app::CmdStatus::Ack : app::CmdStatus::Nack;
      e.cmd.applied_value = c.applied_value;
      strlcpy(e.cmd.action, c.action, sizeof(e.cmd.action));
      strlcpy(e.cmd.error, c.error, sizeof(e.cmd.error));
      app::bus::post(e, 20);
      break;
    }
    case proto::MsgType::PairResult:
      on_pair_result(s_msg.auth);
      memset(s_msg.auth.token, 0, sizeof(s_msg.auth.token));
      break;
    case proto::MsgType::AuthResult:
      if (!s_msg.auth.ok) {
        ESP_LOGW(TAG, "agent rejected our credential (%s): pairing required", s_msg.auth.error);
        s_auth_rejected = true;
        s_authenticated = false;
        with_status([](Status &s) { s.authenticated = false; });
        post_link(app::LinkState::Unauthorized);
      }
      break;
    case proto::MsgType::WeatherData:
      s_weather_ok = s_msg.weather.ok;
      if (s_msg.weather.ok) s_wx_rx_at = millis() | 1;
      e.type = app::EventType::WeatherData;
      e.weather = s_msg.weather;
      app::bus::post(e, 20);
      break;
    case proto::MsgType::WeatherResults:
      portENTER_CRITICAL(&s_mux);
      if (s_place_state == kPlacesSearching) {
        s_place_state = s_msg.places.error ? kPlacesFailed : s_msg.places.count;
        memcpy(s_places, s_msg.places.places, sizeof(s_places));
      }
      portEXIT_CRITICAL(&s_mux);
      e.type = app::EventType::PlacesDone;
      app::bus::post(e, 20);
      break;
    case proto::MsgType::CryptoData:
      if (!s_market_buf) break;
      s_cr_answered = true;
      crypto::store_market(*s_market_buf);
      if (!s_market_buf->ok) ESP_LOGW(TAG, "crypto.data error: %s", s_market_buf->error);
      e.type = app::EventType::CryptoData;
      app::bus::post(e, 20);
      break;
    case proto::MsgType::CryptoCandles:
      if (!s_chart_buf) break;
      if (s_ch_waiting && !strcmp(s_chart_buf->pair, s_ch_wait_pair) && s_chart_buf->range == s_ch_wait_range)
        s_ch_waiting = false;
      if (!s_chart_buf->cur[0]) strlcpy(s_chart_buf->cur, s_ch_wait_cur, sizeof(s_chart_buf->cur));
      crypto::store_chart(*s_chart_buf);
      e.type = app::EventType::CryptoChart;
      app::bus::post(e, 20);
      break;
    case proto::MsgType::Error:
      ESP_LOGW(TAG, "agent error %s: %s", s_msg.error.code, s_msg.error.message);
      if (!strcmp(s_msg.error.code, "unknown_type") && strstr(s_msg.error.message, "crypto")) {
        // PC app older than 0.6: no crypto. Stop asking in this session and tell the UI.
        s_cr_old_agent = true;
        fail_crypto("pc_app_old");
      }
      if (!strcmp(s_msg.error.code, "unknown_type") && strstr(s_msg.error.message, "weather")) {
        // PC app older than 0.5: no weather. Tell the UI once per request.
        e.type = app::EventType::WeatherData;
        e.weather = telemetry::Weather{};
        strlcpy(e.weather.error, "pc_app_old", sizeof(e.weather.error));
        app::bus::post(e, 20);
        portENTER_CRITICAL(&s_mux);
        const bool searching = s_place_state == kPlacesSearching;
        if (searching) s_place_state = kPlacesFailed;
        portEXIT_CRITICAL(&s_mux);
        if (searching) {
          e.type = app::EventType::PlacesDone;
          app::bus::post(e, 20);
        }
      }
      break;
    default:
      break;
  }
}

// ---- album art: binary "PCDA" chunks assembled in PSRAM -------------------------------------------
constexpr size_t kArtHeader = 20;  // magic[4] art_id u32 w u16 h u16 offset u32 total u32 (LE)
constexpr size_t kArtMax = 160 * 160 * 2;
uint8_t *s_art_buf = nullptr;
uint32_t s_art_id = 0;
uint32_t s_art_received = 0;

uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t rd16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void handle_binary(const uint8_t *p, size_t len) {
  if (!s_authenticated || len <= kArtHeader || memcmp(p, "PCDA", 4) != 0) return;
  const uint32_t art_id = rd32(p + 4);
  const uint16_t w = rd16(p + 8), h = rd16(p + 10);
  const uint32_t off = rd32(p + 12), total = rd32(p + 16);
  const size_t n = len - kArtHeader;
  if (total != static_cast<uint32_t>(w) * h * 2 || total > kArtMax || off + n > total) {
    ESP_LOGW(TAG, "art chunk rejected (w=%u h=%u off=%u total=%u)", w, h, static_cast<unsigned>(off), static_cast<unsigned>(total));
    return;
  }
  if (!s_art_buf) s_art_buf = static_cast<uint8_t *>(heap_caps_malloc(kArtMax, MALLOC_CAP_SPIRAM));
  if (!s_art_buf) return;
  if (art_id != s_art_id || off == 0) {
    s_art_id = art_id;
    s_art_received = 0;
  }
  memcpy(s_art_buf + off, p + kArtHeader, n);
  s_art_received += n;
  if (s_art_received < total) return;
  // Complete: hand a private copy to the UI (it frees the previous one once replaced).
  uint8_t *copy = static_cast<uint8_t *>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM));
  if (!copy) return;
  memcpy(copy, s_art_buf, total);
  app::Event e;
  e.type = app::EventType::ArtReady;
  e.art = app::ArtFrame{art_id, w, h, copy};
  if (!app::bus::post(e, 50)) {
    heap_caps_free(copy);
  } else {
    s_art_have = art_id;
  }
  s_art_received = 0;
  ESP_LOGI(TAG, "album art %08x received (%ux%u)", static_cast<unsigned>(art_id), w, h);
}

void post_chart_error(const char *error) {
  crypto::reset(*s_chart_buf);
  strlcpy(s_chart_buf->pair, s_ch_wait_pair, sizeof(s_chart_buf->pair));
  strlcpy(s_chart_buf->cur, s_ch_wait_cur, sizeof(s_chart_buf->cur));
  s_chart_buf->range = s_ch_wait_range;
  strlcpy(s_chart_buf->error, error, sizeof(s_chart_buf->error));
  crypto::store_chart(*s_chart_buf);
  app::Event e;
  e.type = app::EventType::CryptoChart;
  app::bus::post(e, 20);
}

// Market (and pending chart) failure reported to the UI, e.g. an old PC app.
void fail_crypto(const char *error) {
  if (!s_market_buf || !s_chart_buf) return;
  crypto::reset(*s_market_buf);
  strlcpy(s_market_buf->error, error, sizeof(s_market_buf->error));
  crypto::store_market(*s_market_buf);
  app::Event e;
  e.type = app::EventType::CryptoData;
  app::bus::post(e, 20);
  if (s_ch_waiting) {
    s_ch_waiting = false;
    post_chart_error(error);
  }
}

bool agent_live() { return s_ws_open && s_authenticated && s_caps_received; }
bool web_ready() { return WiFi.status() == WL_CONNECTED && !s_pairing; }

void post_market() {
  crypto::store_market(*s_market_buf);
  post_simple(app::EventType::CryptoData);
  s_web_post_at = millis();
  s_web_dirty = false;
}

// Crypto without the PC app: the chart asked for first, then prices every period, then one slower
// refresh per loop (sparklines, market caps...) so the list shows up at once and fills in.
void step_crypto_web(uint32_t now) {
  portENTER_CRITICAL(&s_mux);
  const bool chart = s_ch_pending;
  const int8_t coin = s_ch_coin;
  const uint8_t range = s_ch_range;
  char ccur[4];
  memcpy(ccur, s_ch_cur, sizeof(ccur));
  s_ch_pending = false;
  portEXIT_CRITICAL(&s_mux);
  s_ch_waiting = false;
  if (chart && coin >= 0 && coin < COIN_CATALOG_COUNT) {
    web::chart(COIN_CATALOG[coin].pair, range, ccur, *s_chart_buf);
    if (!s_chart_buf->ok) ESP_LOGW(TAG, "crypto chart (direct) %s: %s", COIN_CATALOG[coin].pair, s_chart_buf->error);
    crypto::store_chart(*s_chart_buf);
    post_simple(app::EventType::CryptoChart);
    return;
  }
  portENTER_CRITICAL(&s_mux);
  const bool active = s_cr_active && s_cr_n > 0;
  const uint32_t period = s_cr_period;
  int8_t idx[crypto::kMaxCoins];
  const int n = s_cr_n;
  memcpy(idx, s_cr_idx, sizeof(idx));
  char cur[4];
  memcpy(cur, s_cr_cur, sizeof(cur));
  portEXIT_CRITICAL(&s_mux);
  if (!active) return;
  const uint32_t every = s_web_cr_ok ? period : kCryptoRetryMs;
  const bool due = s_cr_force || !s_cr_at || now - s_cr_at >= every;
  if (due && (!s_cr_at || now - s_cr_at >= kCryptoMinGapMs)) {
    s_cr_force = false;
    s_cr_at = now;
    s_web_cr_ok = web::prices(idx, n, cur);
    web::build(idx, n, cur, *s_market_buf);
    if (!s_web_cr_ok) {  // keep the last good prices (and their age) in the store
      s_market_buf->ok = false;
      strlcpy(s_market_buf->error, "unavailable", sizeof(s_market_buf->error));
      ESP_LOGW(TAG, "crypto prices (direct): unavailable");
    }
    post_market();
    s_web_enrich = s_web_cr_ok;
    return;
  }
  if (!s_web_enrich) return;
  const bool did = web::enrich(idx, n);
  if (did) s_web_dirty = true;
  if (s_web_dirty && (!did || millis() - s_web_post_at >= 1500)) {
    web::build(idx, n, cur, *s_market_buf);
    post_market();
  }
  if (!did) s_web_enrich = false;
}

void step_crypto() {
  const uint32_t now = millis();
  if (!s_market_buf || !s_chart_buf) return;
  if (!agent_live()) {
    if (web_ready()) step_crypto_web(now);
    return;
  }
  if (s_ch_waiting && now - s_ch_sent_at > kChartTimeoutMs) {
    ESP_LOGW(TAG, "crypto chart %s/%s: no answer", s_ch_wait_pair, crypto::range_code(s_ch_wait_range));
    s_ch_waiting = false;
    post_chart_error("timeout");  // only the chart failed: the prices stay
  }
  if (s_cr_old_agent) {
    portENTER_CRITICAL(&s_mux);
    const bool pending = s_ch_pending;
    s_ch_pending = false;
    portEXIT_CRITICAL(&s_mux);
    if (pending) fail_crypto("pc_app_old");
    return;
  }
  // Chart first: the user is waiting for it.
  portENTER_CRITICAL(&s_mux);
  const bool chart = s_ch_pending && (!s_ch_sent_at || now - s_ch_sent_at >= kChartGapMs);
  const int8_t coin = s_ch_coin;
  const uint8_t range = s_ch_range;
  char ccur[4];
  memcpy(ccur, s_ch_cur, sizeof(ccur));
  if (chart) s_ch_pending = false;
  portEXIT_CRITICAL(&s_mux);
  if (chart && coin >= 0 && coin < COIN_CATALOG_COUNT) {
    const char *pair = COIN_CATALOG[coin].pair;
    send_json(s_cr_tx, proto::build_crypto_chart(s_cr_tx, sizeof(s_cr_tx), pair, range, ccur));
    s_ch_waiting = true;
    s_ch_sent_at = now;
    strlcpy(s_ch_wait_pair, pair, sizeof(s_ch_wait_pair));
    s_ch_wait_range = range;
    memcpy(s_ch_wait_cur, ccur, sizeof(s_ch_wait_cur));
    return;
  }
  // Prices.
  portENTER_CRITICAL(&s_mux);
  const bool active = s_cr_active && s_cr_n > 0;
  const uint32_t period = s_cr_period;
  int8_t idx[crypto::kMaxCoins];
  const int n = s_cr_n;
  memcpy(idx, s_cr_idx, sizeof(idx));
  char cur[4];
  memcpy(cur, s_cr_cur, sizeof(cur));
  portEXIT_CRITICAL(&s_mux);
  if (!active) return;
  const uint32_t every = s_cr_answered ? period : kCryptoRetryMs;
  const bool due = s_cr_force || !s_cr_at || now - s_cr_at >= every;
  if (!due || (s_cr_at && now - s_cr_at < kCryptoMinGapMs)) return;
  s_cr_force = false;
  s_cr_at = now;
  const char *pairs[crypto::kMaxCoins];
  const char *ids[crypto::kMaxCoins];
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (idx[i] < 0 || idx[i] >= COIN_CATALOG_COUNT) continue;
    pairs[k] = COIN_CATALOG[idx[i]].pair;
    ids[k] = COIN_CATALOG[idx[i]].cg_id;
    k++;
  }
  send_json(s_cr_tx, proto::build_crypto_request(s_cr_tx, sizeof(s_cr_tx), pairs, ids, k, cur));
}

// Forecast without the PC app: every 10 min (a minute after a failure), or at once for a new place.
void step_weather_web(uint32_t now) {
  portENTER_CRITICAL(&s_mux);
  const bool set = s_place_set;
  const float lat = s_place_lat, lon = s_place_lon;
  portEXIT_CRITICAL(&s_mux);
  if (!set) return;
  const bool stale = !s_wx_rx_at || now - s_wx_rx_at >= kWeatherEveryMs;
  const bool may = !s_web_wx_at || now - s_web_wx_at >= kWebWeatherRetryMs;
  if (!s_weather_force && !(stale && may)) return;
  s_weather_force = false;
  s_web_wx_at = now | 1;
  telemetry::Weather w;
  web::forecast(lat, lon, w);
  if (w.ok) s_wx_rx_at = millis() | 1;
  ESP_LOGI(TAG, "weather (direct): %s", w.ok ? "ok" : w.error);
  app::Event e;
  e.type = app::EventType::WeatherData;
  e.weather = w;
  app::bus::post(e, 50);
}

void step_weather() {
  const uint32_t now = millis();
  const bool live = s_ws_open && s_authenticated && s_caps_received;
  // City search.
  portENTER_CRITICAL(&s_mux);
  const bool search = s_search_requested;
  char q[sizeof(s_search_q)], lang[sizeof(s_search_lang)];
  memcpy(q, s_search_q, sizeof(q));
  memcpy(lang, s_search_lang, sizeof(lang));
  const bool timed_out = s_place_state == kPlacesSearching && !search && now - s_search_at > kSearchTimeoutMs;
  if (timed_out) s_place_state = kPlacesFailed;
  portEXIT_CRITICAL(&s_mux);
  app::Event e;
  if (timed_out) {
    e.type = app::EventType::PlacesDone;
    app::bus::post(e, 20);
  }
  if (search && (!live || now - s_weather_msg_at >= kWeatherGapMs)) {
    const bool web = !live && web_ready();
    portENTER_CRITICAL(&s_mux);
    s_search_requested = false;
    s_search_at = now;
    if (!live && !web) s_place_state = kPlacesOffline;
    portEXIT_CRITICAL(&s_mux);
    if (web) {  // no PC app: ask Open-Meteo directly
      telemetry::Place found[telemetry::kMaxPlaces];
      const int n = web::search(q, lang, found, telemetry::kMaxPlaces);
      portENTER_CRITICAL(&s_mux);
      if (s_place_state == kPlacesSearching && !s_search_requested) {  // not overtaken by a newer search
        s_place_state = n < 0 ? kPlacesFailed : n;
        if (n > 0) memcpy(s_places, found, sizeof(telemetry::Place) * n);
      }
      portEXIT_CRITICAL(&s_mux);
    }
    if (live) {
      char buf[160];
      send_json(buf, proto::build_weather_search(buf, sizeof(buf), q, lang));
      s_weather_msg_at = now;
    } else {
      e.type = app::EventType::PlacesDone;
      app::bus::post(e, 20);
    }
  }
  // Forecast for the chosen place.
  if (!live) {
    if (web_ready()) step_weather_web(now);
    return;
  }
  portENTER_CRITICAL(&s_mux);
  const bool set = s_place_set;
  const float lat = s_place_lat, lon = s_place_lon;
  portEXIT_CRITICAL(&s_mux);
  if (!set || now - s_weather_msg_at < kWeatherGapMs) return;
  const uint32_t every = s_weather_ok ? kWeatherEveryMs : kWeatherRetryMs;
  if (s_weather_force || !s_weather_at || now - s_weather_at >= every) {
    s_weather_force = false;
    s_weather_at = now;
    s_weather_msg_at = now;
    char buf[128];
    send_json(buf, proto::build_weather_request(buf, sizeof(buf), lat, lon));
  }
}

void check_art(uint32_t wanted) {
  const uint32_t now = millis();
  if (wanted == 0 || wanted == s_art_have) {
    s_art_missing_since = 0;
    return;
  }
  if (!s_art_missing_since) s_art_missing_since = now;
  if (now - s_art_missing_since < kArtGraceMs || (s_art_req_at && now - s_art_req_at < kArtRetryMs)) return;
  s_art_req_at = now;
  char buf[96];
  send_json(buf, proto::build_art_request(buf, sizeof(buf), wanted));
  ESP_LOGI(TAG, "album art %08x missing, asked again", static_cast<unsigned>(wanted));
}

void on_ws_event(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_BIN:
      handle_binary(payload, length);
      break;
    case WStype_CONNECTED: {
      s_ws_open = true;
      s_caps_received = false;
      s_authenticated = false;
      s_attempt = 0;
      s_fail_streak = 0;
      s_ws.setReconnectInterval(kBackoffMinMs);
      with_status([](Status &s) {
        s.ws_connected = true;
        s.authenticated = false;
        s.backoff_ms = 0;
      });
      char buf[200];
      send_json(buf, proto::build_hello(buf, sizeof(buf), s_client_id, PCD_FW_VERSION));
      if (s_pairing) {
        send_json(buf, proto::build_pair_request(buf, sizeof(buf), s_pair_code, s_device_name));
        ESP_LOGI(TAG, "websocket open -> hello + pair.request");
      } else if (s_cfg.paired()) {
        send_json(buf, proto::build_auth_request(buf, sizeof(buf), s_cfg.token));
        ESP_LOGI(TAG, "websocket open -> hello + auth.request");
      } else {
        ESP_LOGI(TAG, "websocket open -> hello (not paired)");
      }
      memset(buf, 0, sizeof(buf));  // may have held the token
      s_last_hb = millis();
      break;
    }
    case WStype_DISCONNECTED: {
      const bool was_open = s_ws_open;
      s_ws_open = false;
      s_caps_received = false;
      s_authenticated = false;
      s_art_have = 0;  // a new session gets the current cover again
      s_weather_ok = false;
      s_weather_at = 0;
      s_cr_answered = false;
      s_cr_old_agent = false;
      s_cr_at = 0;
      if (s_ch_waiting) {  // the answer can no longer arrive: ask again in the next session
        s_ch_waiting = false;
        portENTER_CRITICAL(&s_mux);
        s_ch_pending = true;
        portEXIT_CRITICAL(&s_mux);
      }
      s_art_missing_since = 0;
      s_art_req_at = 0;
      // Observed on hardware: after some resets the station reports CONNECTED but no packet gets
      // through. Several consecutive failed connects -> try mDNS (PC IP may have changed), then
      // rebuild the Wi-Fi association.
      if (!was_open && ++s_fail_streak >= kMaxFailStreak) {
        s_fail_streak = 0;
        s_force_wifi_reconnect = true;
      }
      const uint32_t wait = next_backoff();
      s_ws.setReconnectInterval(wait);
      with_status([wait, was_open](Status &s) {
        s.ws_connected = false;
        s.authenticated = false;
        s.backoff_ms = wait;
        if (was_open) s.reconnects++;
      });
      if (was_open) ESP_LOGW(TAG, "websocket closed; retry in %u ms", static_cast<unsigned>(wait));
      if (!s_auth_rejected) post_link(app::LinkState::Connecting);
      break;
    }
    case WStype_TEXT:
      if (length > proto::kMaxFrame) {
        with_status([](Status &s) { s.rx_rejected++; });
        break;
      }
      handle_text(payload, length);
      break;
    case WStype_ERROR:
      ESP_LOGW(TAG, "websocket error");
      break;
    default:
      break;
  }
}

void stop_ws() {
  if (s_ws_started) {
    s_ws.disconnect();
    s_ws_started = false;
  }
  s_ws_open = false;
  s_caps_received = false;
  s_authenticated = false;
}

void radio_off() {
  stop_ws();
  web::idle(true);
  if (s_mdns_started) {
    MDNS.end();
    s_mdns_started = false;
  }
#if PCD_WIFI_ESP_HOSTED
  // ESP32-P4: the radio is the ESP32-C6 behind ESP-Hosted. WIFI_OFF tears that SDIO link down and
  // bringing it back fails ("card init failed", esp_wifi_init ESP_FAIL), so once the link is up the
  // deck only disconnects and stays in STA mode.
  if (WiFi.getMode() != WIFI_OFF) WiFi.disconnect(false, false);
#else
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
  }
#endif
  s_wifi_begun = false;
  with_status([](Status &s) {
    s.wifi_connected = false;
    s.ws_connected = false;
    s.authenticated = false;
    s.ip[0] = '\0';
    s.rssi = 0;
  });
}

// ---- Wi-Fi scan -------------------------------------------------------------------------------
void collect_scan(int n) {
  ScanEntry tmp[kMaxScan];
  int count = 0;
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || ssid.length() > 32) continue;  // hidden or invalid
    const int8_t rssi = static_cast<int8_t>(WiFi.RSSI(i));
    const bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    int dup = -1;
    for (int k = 0; k < count; k++)
      if (strcmp(tmp[k].ssid, ssid.c_str()) == 0) dup = k;
    if (dup >= 0) {
      if (rssi > tmp[dup].rssi) tmp[dup].rssi = rssi;
      continue;
    }
    if (count >= kMaxScan) continue;
    strlcpy(tmp[count].ssid, ssid.c_str(), sizeof(tmp[count].ssid));
    tmp[count].rssi = rssi;
    tmp[count].secure = secure;
    count++;
  }
  for (int i = 1; i < count; i++) {  // insertion sort by RSSI, strongest first
    ScanEntry key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j].rssi < key.rssi) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = key;
  }
  portENTER_CRITICAL(&s_mux);
  memcpy(s_scan, tmp, sizeof(tmp));
  s_scan_count = count;
  portEXIT_CRITICAL(&s_mux);
}

// Returns true while a scan owns the radio and the normal state machine must not run.
bool step_scan() {
#if PCD_WIFI_ESP_HOSTED
  if (s_scan_requested && !s_scanning && static_cast<int32_t>(millis() - s_scan_retry_at) >= 0) {
#else
  if (s_scan_requested && !s_scanning) {
#endif
    s_scan_requested = false;
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);  // scanning needs the radio, even in demo mode
#if PCD_WIFI_ESP_HOSTED
    // A connection attempt in progress makes the C6's scan fail: pause it (it resumes after the scan).
    if (s_wifi_begun && WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(false, false);
      s_wifi_begun = false;
    }
#endif
    WiFi.scanDelete();
#if PCD_WIFI_ESP_HOSTED
    // Active scan, 300 ms per channel (same parameters as the JC1060P470C CryptoTracker).
    if (WiFi.scanNetworks(true /*async*/, false /*show hidden*/, false /*passive*/, 300) == WIFI_SCAN_FAILED) {
      if (++s_scan_tries < 4) {  // right after the radio comes up the C6 can refuse it: retry shortly
        ESP_LOGW(TAG, "Wi-Fi scan start failed, retry %u", s_scan_tries);
        s_scan_requested = true;
        s_scan_retry_at = millis() + 700;
        return false;
      }
      s_scan_tries = 0;
#else
    if (WiFi.scanNetworks(true /*async*/, false /*show hidden*/) == WIFI_SCAN_FAILED) {
#endif
      portENTER_CRITICAL(&s_mux);
      s_scan_count = -1;
      portEXIT_CRITICAL(&s_mux);
      post_simple(app::EventType::WifiScanDone);
    } else {
#if PCD_WIFI_ESP_HOSTED
      s_scan_tries = 0;
#endif
      s_scanning = true;
      s_scan_start = millis();
      with_status([](Status &s) { s.scanning = true; });
      ESP_LOGI(TAG, "Wi-Fi scan started");
    }
  }
  if (!s_scanning) return false;
  const int r = WiFi.scanComplete();
  if (r == WIFI_SCAN_RUNNING && millis() - s_scan_start < 15000) {
    return !(s_enabled && WiFi.status() == WL_CONNECTED);
  }
  if (r >= 0) {
    collect_scan(r);
    ESP_LOGI(TAG, "Wi-Fi scan: %d AP(s), %d listed", r, s_scan_count);
  } else {
    portENTER_CRITICAL(&s_mux);
    s_scan_count = -1;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGW(TAG, "Wi-Fi scan failed (%d)", r);
  }
  WiFi.scanDelete();
  s_scanning = false;
  with_status([](Status &s) { s.scanning = false; });
  post_simple(app::EventType::WifiScanDone);
  return false;
}

// ---- mDNS -------------------------------------------------------------------------------------
bool ensure_mdns() {
  if (s_mdns_started) return true;
  char host[24];
  snprintf(host, sizeof(host), "pcdeck-%s", s_client_id + strlen(s_client_id) - 6);
  s_mdns_started = MDNS.begin(host);
  if (!s_mdns_started) ESP_LOGW(TAG, "mDNS start failed");
  return s_mdns_started;
}

// Blocking browse (~1-3 s), runs in this task only.
int browse(PcEntry *out, int max) {
  if (!ensure_mdns()) return kDiscoveryFailed;
  const int n = MDNS.queryService("pcdeck", "tcp");
  int count = 0;
  for (int i = 0; i < n && count < max; i++) {
    const IPAddress ip = MDNS.address(i);
    if (ip == IPAddress(0, 0, 0, 0)) continue;
    PcEntry &e = out[count++];
    e = PcEntry{};
    strlcpy(e.host, ip.toString().c_str(), sizeof(e.host));
    e.port = MDNS.port(i);
    String name = MDNS.hasTxt(i, "name") ? MDNS.txt(i, "name") : MDNS.hostname(i);
    strlcpy(e.name, name.c_str(), sizeof(e.name));
    if (MDNS.hasTxt(i, "id")) strlcpy(e.agent_id, MDNS.txt(i, "id").c_str(), sizeof(e.agent_id));
  }
  ESP_LOGI(TAG, "mDNS: %d PC agent(s) found", count);
  return count;
}

void step_discovery() {
  if (!s_discover_requested) return;
  s_discover_requested = false;
  int n;
  PcEntry tmp[kMaxPcs];
  if (WiFi.status() != WL_CONNECTED) {
    n = kDiscoveryNoWifi;
  } else {
    with_status([](Status &s) { s.discovering = true; });
    n = browse(tmp, kMaxPcs);
    with_status([](Status &s) { s.discovering = false; });
  }
  portENTER_CRITICAL(&s_mux);
  if (n > 0) memcpy(s_pcs, tmp, sizeof(PcEntry) * n);
  s_pc_count = n;
  portEXIT_CRITICAL(&s_mux);
  post_simple(app::EventType::DiscoveryDone);
}

// PC address may have changed (DHCP): look our paired agent up by id. Returns true if updated.
bool relocate_agent() {
  if (!s_cfg.agent_id[0]) return false;
  PcEntry found[kMaxPcs];
  const int n = browse(found, kMaxPcs);
  for (int i = 0; i < n; i++) {
    if (strcmp(found[i].agent_id, s_cfg.agent_id) != 0) continue;
    if (strcmp(found[i].host, s_cfg.host) == 0 && found[i].port == s_cfg.port) return false;
    ESP_LOGW(TAG, "paired PC moved: %s:%u -> %s:%u", s_cfg.host, s_cfg.port, found[i].host, found[i].port);
    storage::NetConfig cfg;
    storage::load_net(cfg);
    strlcpy(cfg.host, found[i].host, sizeof(cfg.host));
    cfg.port = found[i].port;
    storage::save_net(cfg);
    strlcpy(s_cfg.host, found[i].host, sizeof(s_cfg.host));
    s_cfg.port = found[i].port;
    memset(&cfg, 0, sizeof(cfg));
    publish_cfg_status();
    return true;
  }
  return false;
}

// ---- main state machine ---------------------------------------------------------------------------
void step() {
  if (s_reconfigure) {
    s_reconfigure = false;
    storage::load_net(s_cfg);
    radio_off();
    s_auth_rejected = false;
    publish_cfg_status();
    ESP_LOGI(TAG, "config: ssid='%s' (password %s) agent=%s:%u paired=%d", s_cfg.ssid, s_cfg.pass[0] ? "set" : "empty",
             s_cfg.host, s_cfg.port, s_cfg.paired());
    s_pending_reason = 0;
    s_attempt_start_ms = 0;
    with_status([](Status &s) { s.last_disconnect_reason = 0; });
  }
  if (step_scan()) return;
  if (!s_enabled || !s_cfg.has_wifi()) {
    radio_off();
    post_link(app::LinkState::NoWifi);
    if (s_discover_requested) step_discovery();  // answers kDiscoveryNoWifi
    if (s_pair_requested) {
      s_pair_requested = false;
      s_pairing = true;
      finish_pairing(false, "no_wifi", "");
    }
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (s_ws_started) stop_ws();
    web::idle(true);
    with_status([](Status &s) {
      s.wifi_connected = false;
      s.ws_connected = false;
    });
    s_dns_settled = false;
    post_link(app::LinkState::NoWifi);
    if (!s_wifi_begun || millis() - s_wifi_begin_ms > kWifiRetryMs) {
      if (!s_wifi_begun && s_attempt_start_ms == 0) s_attempt_start_ms = millis() | 1;
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      WiFi.begin(s_cfg.ssid, s_cfg.pass);
      s_wifi_begun = true;
      s_wifi_begin_ms = millis();
      ESP_LOGI(TAG, "connecting to Wi-Fi '%s'", s_cfg.ssid);
    }
    // Still not in after kFailShowAfterMs: now the reason means something to the user.
    const uint8_t pending = s_pending_reason;
    const uint32_t started = s_attempt_start_ms;
    if (pending && started && millis() - started >= kFailShowAfterMs)
      with_status([pending](Status &s) { s.last_disconnect_reason = pending; });
    if (s_discover_requested) step_discovery();
    return;
  }

  char ip[sizeof(Status::ip)];  // built outside the critical section (no allocation with IRQs masked)
  strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
  const int8_t rssi = static_cast<int8_t>(WiFi.RSSI());
  with_status([&ip, rssi](Status &s) {
    s.wifi_connected = true;
    s.rssi = rssi;
    memcpy(s.ip, ip, sizeof(s.ip));
  });

  if (!s_dns_settled) {
    s_dns_settled = true;
    settle_dns();
  }
  if (!s_sntp_started) {
    s_sntp_started = true;
    configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC; the UI applies the offset
  }
  step_discovery();

  if (s_pair_requested) {
    // Switch the connection to the selected PC and send pair.request as soon as it opens.
    s_pair_requested = false;
    stop_ws();
    portENTER_CRITICAL(&s_mux);
    strlcpy(s_cfg.host, s_pair_target.host, sizeof(s_cfg.host));
    s_cfg.port = s_pair_target.port;
    portEXIT_CRITICAL(&s_mux);
    s_pairing = true;
    s_auth_rejected = false;
    s_pair_deadline = millis() + kPairTimeoutMs;
    with_status([](Status &s) { s.pairing = true; });
    post_link(app::LinkState::Pairing);
    ESP_LOGI(TAG, "pairing with %s:%u", s_cfg.host, s_cfg.port);
  }
  if (s_pairing && static_cast<int32_t>(millis() - s_pair_deadline) >= 0) {
    finish_pairing(false, s_ws_open ? "timeout" : "unreachable", "");
    if (!s_cfg.paired()) post_link(app::LinkState::Discovering);
  }

  if (s_force_wifi_reconnect) {
    s_force_wifi_reconnect = false;
    if (relocate_agent()) {
      stop_ws();  // reconnect to the new address
    } else if (web::answered_within(3 * 60 * 1000)) {
      // The internet answers over this Wi-Fi: the PC is just off or its app closed. Rebuilding the
      // association would only interrupt the weather and crypto the deck fetches meanwhile.
      ESP_LOGI(TAG, "agent unreachable %u times, internet fine: keeping Wi-Fi", kMaxFailStreak);
    } else {
      ESP_LOGW(TAG, "agent unreachable %u times while Wi-Fi is up: rebuilding the Wi-Fi association", kMaxFailStreak);
      stop_ws();
      WiFi.disconnect(false, false);
      s_wifi_begun = false;
      return;
    }
  }

  if (!s_cfg.has_agent() || (!s_cfg.paired() && !s_pairing) || (s_agent_paused && !s_pairing)) {
    if (s_ws_started) stop_ws();
    post_link(app::LinkState::Discovering);  // Wi-Fi OK, no paired PC: user should pair
    step_weather();  // weather and crypto straight from the internet meanwhile
    step_crypto();
    web::idle(false);
    return;
  }
  if (s_auth_rejected && !s_pairing) {
    if (s_ws_started) stop_ws();
    post_link(app::LinkState::Unauthorized);
    step_weather();
    step_crypto();
    web::idle(false);
    return;
  }
  if (!s_ws_started) {
    ESP_LOGI(TAG, "Wi-Fi up (%s), connecting to ws://%s:%u/", ip, s_cfg.host, s_cfg.port);
    s_ws.begin(s_cfg.host, s_cfg.port, "/", "");  // empty subprotocol: no Sec-WebSocket-Protocol header
    s_ws.onEvent(on_ws_event);
    s_ws.setReconnectInterval(kBackoffMinMs);
    s_ws.enableHeartbeat(15000, 5000, 2);  // WS-level ping/pong
    s_ws_started = true;
    if (!s_pairing) post_link(app::LinkState::Connecting);
  }
  s_ws.loop();
  // Outbound commands from the UI (only an authenticated session may carry them).
  commands::Request req;
  while (commands::next_outbound(req, 0)) {
    char buf[200];
    const size_t n = (s_ws_open && s_authenticated)
                         ? proto::build_command(buf, sizeof(buf), req.request_id, commands::action_name(req.action), req.value)
                         : 0;
    if (n) {
      s_ws.sendTXT(buf, n);
      ESP_LOGI(TAG, "-> command #%u %s", static_cast<unsigned>(req.request_id), commands::action_name(req.action));
    } else {
      app::Event e;  // immediate NACK instead of waiting for the timeout
      e.type = app::EventType::CommandResult;
      e.cmd = app::CommandResult{};
      e.cmd.request_id = req.request_id;
      e.cmd.status = app::CmdStatus::Nack;
      strlcpy(e.cmd.action, commands::action_name(req.action), sizeof(e.cmd.action));
      strlcpy(e.cmd.error, "offline", sizeof(e.cmd.error));
      app::bus::post(e, 20);
    }
  }
  if (s_ws_open && millis() - s_last_hb >= kHeartbeatMs) {
    s_last_hb = millis();
    char hb[64];
    send_json(hb, proto::build_heartbeat(hb, sizeof(hb)));
  }
  step_weather();
  step_crypto();
  web::idle(false);
  if (s_ws_open && !s_caps_received && !s_pairing) post_link(app::LinkState::Connecting);
}

void on_wifi_event(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // Kept for later (step() publishes it once the attempt has kept failing); a drop of a working
    // link starts a new attempt.
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    if (reason == kReasonAssocLeave) return;
    if (s_attempt_start_ms == 0) s_attempt_start_ms = millis() | 1;
    s_pending_reason = reason;
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    s_pending_reason = 0;
    s_attempt_start_ms = 0;
    with_status([](Status &s) { s.last_disconnect_reason = 0; });
  }
}

void task(void *) {
  proto::set_boot_nonce(esp_random());
  WiFi.onEvent(on_wifi_event);
  uint8_t mac[6] = {};
#if PCD_WIFI_ESP_HOSTED
  // The ESP32-P4 has no Wi-Fi MAC of its own (the C6 has): the deck identity uses the P4 base MAC.
  esp_efuse_mac_get_default(mac);
#else
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
  snprintf(s_client_id, sizeof(s_client_id), "deck-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  snprintf(s_device_name, sizeof(s_device_name), "DECKY %02X%02X", mac[4], mac[5]);
#if PCD_WIFI_ESP_HOSTED
  // ESP32-P4: the radio is the ESP32-C6 behind ESP-Hosted (SDIO). Bring it up once, in STA mode and
  // without power save, and keep it up (radio_off() only disconnects): scans and connections then
  // work at any time. Same start-up as the JC1060P470C CryptoTracker.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  ESP_LOGI(TAG, "Wi-Fi (ESP32-C6 via ESP-Hosted) up, client %s", s_client_id);
#endif
  for (;;) {
    step();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

bool start() {
  if (s_task) return true;
  crypto::store_init();
  void *mb = heap_caps_malloc(sizeof(crypto::Market), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  void *cb = heap_caps_malloc(sizeof(crypto::Chart), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (mb && cb) {
    s_market_buf = static_cast<crypto::Market *>(mb);
    s_chart_buf = static_cast<crypto::Chart *>(cb);
    crypto::reset(*s_market_buf);
    crypto::reset(*s_chart_buf);
    s_msg.market = s_market_buf;
    s_msg.chart = s_chart_buf;
  }
  // 16 KB: ArduinoJson documents, mDNS browse results, the WebSocket client and the TLS handshakes of
  // the direct internet requests (web_source); the snapshot buffer is static.
  return xTaskCreatePinnedToCore(task, "net", 16384, nullptr, 3, &s_task, 0) == pdPASS;
}

void set_enabled(bool on) { s_enabled = on; }
void reconfigure() { s_reconfigure = true; }
void request_scan() { s_scan_requested = true; }
void request_discovery() { s_discover_requested = true; }

void set_weather_place(bool set, float lat, float lon) {
  portENTER_CRITICAL(&s_mux);
  s_place_set = set;
  s_place_lat = lat;
  s_place_lon = lon;
  portEXIT_CRITICAL(&s_mux);
  s_weather_force = true;
}

void request_weather() { s_weather_force = true; }

void set_crypto(const int8_t *coins, int n, const char *cur, uint32_t period_ms, bool active) {
  if (n > crypto::kMaxCoins) n = crypto::kMaxCoins;
  if (n < 0) n = 0;
  portENTER_CRITICAL(&s_mux);
  const bool changed = n != s_cr_n || memcmp(coins, s_cr_idx, n) != 0 || strncmp(cur, s_cr_cur, 3) != 0 ||
                       (active && !s_cr_active);
  memcpy(s_cr_idx, coins, n);
  s_cr_n = static_cast<uint8_t>(n);
  strlcpy(s_cr_cur, cur, sizeof(s_cr_cur));
  s_cr_period = period_ms < 10000 ? 10000 : period_ms;
  s_cr_active = active;
  portEXIT_CRITICAL(&s_mux);
  if (changed) s_cr_force = true;
}

void request_crypto() { s_cr_force = true; }

void set_agent_paused(bool paused) {
  s_agent_paused = paused;
  ESP_LOGI(TAG, "PC app %s (USB console)", paused ? "ignored" : "used again");
}

void request_crypto_chart(int coin, uint8_t range, const char *cur) {
  portENTER_CRITICAL(&s_mux);
  s_ch_coin = static_cast<int8_t>(coin);
  s_ch_range = range;
  strlcpy(s_ch_cur, cur, sizeof(s_ch_cur));
  s_ch_pending = true;
  portEXIT_CRITICAL(&s_mux);
}

void search_places(const char *query, const char *lang) {
  portENTER_CRITICAL(&s_mux);
  strlcpy(s_search_q, query, sizeof(s_search_q));
  strlcpy(s_search_lang, lang, sizeof(s_search_lang));
  s_search_requested = true;
  s_place_state = kPlacesSearching;
  portEXIT_CRITICAL(&s_mux);
}

int place_results(telemetry::Place *out, int max) {
  portENTER_CRITICAL(&s_mux);
  const int st = s_place_state;
  const int n = st < 0 ? st : (st < max ? st : max);
  if (n > 0) memcpy(out, s_places, sizeof(telemetry::Place) * n);
  portEXIT_CRITICAL(&s_mux);
  return n;
}

int scan_results(ScanEntry *out, int max) {
  portENTER_CRITICAL(&s_mux);
  const int n = s_scan_count < 0 ? -1 : (s_scan_count < max ? s_scan_count : max);
  if (n > 0) memcpy(out, s_scan, sizeof(ScanEntry) * n);
  portEXIT_CRITICAL(&s_mux);
  return n;
}

int discovery_results(PcEntry *out, int max) {
  portENTER_CRITICAL(&s_mux);
  const int n = s_pc_count < 0 ? s_pc_count : (s_pc_count < max ? s_pc_count : max);
  if (n > 0) memcpy(out, s_pcs, sizeof(PcEntry) * n);
  portEXIT_CRITICAL(&s_mux);
  return n;
}

void start_pairing(const PcEntry &pc, const char *code) {
  portENTER_CRITICAL(&s_mux);
  s_pair_target = pc;
  strlcpy(s_pair_code, code, sizeof(s_pair_code));
  portEXIT_CRITICAL(&s_mux);
  s_pair_requested = true;
}

void forget_pc() {
  storage::NetConfig cfg;
  storage::load_net(cfg);
  cfg.host[0] = '\0';
  memset(cfg.token, 0, sizeof(cfg.token));
  cfg.agent_id[0] = '\0';
  cfg.agent_name[0] = '\0';
  storage::save_net(cfg);
  memset(&cfg, 0, sizeof(cfg));
  s_reconfigure = true;
}

Failure classify(uint8_t reason) {
  switch (reason) {
    case 0: return Failure::None;
    case 2:    // AUTH_EXPIRE
    case 15:   // 4WAY_HANDSHAKE_TIMEOUT (typical wrong WPA2 password)
    case 202:  // AUTH_FAIL
    case 204:  // HANDSHAKE_TIMEOUT
      return Failure::WrongPassword;
    case 201:  // NO_AP_FOUND
      return Failure::NotFound;
    default:
      return Failure::Other;
  }
}

Status status() {
  Status copy;
  portENTER_CRITICAL(&s_mux);
  copy = s_status;
  portEXIT_CRITICAL(&s_mux);
  return copy;
}

}  // namespace net
