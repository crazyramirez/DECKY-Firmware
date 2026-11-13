#include "app/app_controller.h"

#include <Arduino.h>
#include <WiFi.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <lvgl.h>

#include "app/demo_source.h"
#include "app/event_bus.h"
#include "app/state_store.h"
#include "board/backlight.h"
#include "board/battery.h"
#include "app/power.h"
#include "board/console_io.h"
#include "board/lvgl_port.h"
#include "deck_config.h"
#include "commands/command_client.h"
#include "crypto/crypto_store.h"
#include "crypto/watchlist.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "network/net_task.h"
#include "network/serial_console.h"
#include "storage/net_store.h"
#include "ui/crypto_fmt.h"
#include "ui/i18n.h"
#include "ui/theme/theme.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"
#include "ui/wallpaper.h"

namespace app {
namespace {

const char *TAG = "app";
uint32_t s_last_1s = 0;
uint32_t s_last_diag = 0;
bool s_rebuild_pending = false;
bool s_setup_lit = false;  // the backlight shows the setup assistant's level

void set_link(LinkState l) {
  AppState &st = state();
  if (st.link == l) return;
  ESP_LOGI(TAG, "link %d -> %d", static_cast<int>(st.link), static_cast<int>(l));
  st.link = l;
  ui::on_link();
}

void apply_capabilities() {
  AppState &st = state();
  if (st.settings.demo_mode) {
    // Demo source answers every action with a simulated ACK, incl. power (confirm dialog test).
    st.caps = Capabilities{true, true, true, true};
  } else {
    // Real agent: only what telemetry.capabilities advertises (Hito 3 agent: none, read-only).
    st.caps = Capabilities{};
  }
}

void evaluate_link(uint32_t now) {
  AppState &st = state();
  if (!st.settings.demo_mode && st.net_link != LinkState::Online) {
    // Socket not usable: show what the network task reports (NO WI-FI / CONNECTING / ...).
    // Keep STALE/OFFLINE if we had data before, so a drop is visible as a data-age problem.
    if (st.last_rx_ms != 0 && st.net_link == LinkState::Connecting) {
      set_link(now - st.last_rx_ms >= kOfflineAfterMs ? LinkState::Offline : LinkState::Stale);
    } else {
      set_link(st.net_link);
    }
    return;
  }
  if (st.last_rx_ms == 0) {
    set_link(LinkState::Connecting);
    return;
  }
  const uint32_t age = now - st.last_rx_ms;
  if (age >= kOfflineAfterMs)
    set_link(LinkState::Offline);
  else if (age >= kStaleAfterMs)
    set_link(LinkState::Stale);
  else
    set_link(LinkState::Online);
}

// PC clock: set the system time when it drifts, remember the UTC offset (for the next boot, before
// the PC connects; SNTP gives UTC meanwhile).
void sync_clock(const telemetry::Snapshot &s) {
  if (s.clock_epoch < 1700000000u) return;
  const time_t now = time(nullptr);
  if (llabs(static_cast<long long>(now) - static_cast<long long>(s.clock_epoch)) > 2) {
    timeval tv{static_cast<time_t>(s.clock_epoch), 0};
    settimeofday(&tv, nullptr);
  }
  AppState &st = state();
  if (st.settings.utc_offset_min != s.clock_offset_min) {
    st.settings.utc_offset_min = s.clock_offset_min;
    storage::save(st.settings);
  }
}

void drain_events(uint32_t now) {
  Event e;
  bool telemetry = false;
  int budget = 8;  // bounded work per loop iteration
  while (budget-- > 0 && bus::poll(e)) {
    switch (e.type) {
      case EventType::Telemetry: {
        // Ignore synthetic data that arrives after demo mode was switched off.
        if (e.snapshot.synthetic && !state().settings.demo_mode) break;
        const uint8_t notify = ingest(e.snapshot, now);
        if (!e.snapshot.synthetic) sync_clock(e.snapshot);
        telemetry = true;
        if (notify) {  // a thermal alert must be seen
          ui::screensaver::stop();
          power::screen_on();
        }
        if (notify & 0x01) ui::on_alert(0x01);
        if (notify & 0x02) ui::on_alert(0x02);
        if (notify) ESP_LOGW(TAG, "thermal alert mask=0x%02X", notify);
        break;
      }
      case EventType::LinkChanged:
        state().net_link = e.link;
        evaluate_link(now);
        break;
      case EventType::WifiScanDone:
        ui::on_wifi_scan();
        break;
      case EventType::ArtReady:
        ui::on_art(e.art);  // takes ownership of e.art.data
        break;
      case EventType::DiscoveryDone: {
        net::PcEntry pcs[net::kMaxPcs];
        const int n = net::discovery_results(pcs, net::kMaxPcs);
        ESP_LOGI(TAG, "discovery: %d PC(s)", n);
        for (int i = 0; i < n; i++) ESP_LOGI(TAG, "  %s  %s:%u  id=%s", pcs[i].name, pcs[i].host, pcs[i].port, pcs[i].agent_id);
        ui::on_discovery();
        break;
      }
      case EventType::PairDone:
        ESP_LOGI(TAG, "pairing %s %s", e.pair.ok ? "ok with" : "failed:", e.pair.ok ? e.pair.agent_name : e.pair.error);
        ui::on_pair_done(e.pair);
        break;
      case EventType::AgentCaps: {
        AppState &st = state();
        if (st.settings.demo_mode) break;
        st.caps = Capabilities{e.caps.volume, e.caps.media, e.caps.lock, e.caps.power, e.caps.seek};
        strlcpy(st.agent_provider, e.caps.provider, sizeof(st.agent_provider));
        strlcpy(st.agent_first_note, e.caps.first_note, sizeof(st.agent_first_note));
        st.agent_note_count = e.caps.note_count;
        ESP_LOGI(TAG, "agent caps: %s, %u note(s)", st.agent_provider, st.agent_note_count);
        ui::on_link();
        break;
      }
      case EventType::WeatherData: {
        AppState &st = state();
        if (e.weather.ok) {
          st.weather = e.weather;
          st.weather_rx_ms = now;
          st.weather_error[0] = '\0';
          // No PC clock: the chosen place's UTC offset gives the local time (summer time included).
          const int16_t tz = e.weather.tz_min;
          if (tz != telemetry::kNoTz && tz >= -14 * 60 && tz <= 14 * 60 && st.link != LinkState::Online &&
              st.link != LinkState::Stale && st.settings.utc_offset_min != tz) {
            st.settings.utc_offset_min = tz;
            storage::save(st.settings);
            ESP_LOGI(TAG, "clock: UTC%+d min from the weather place", tz);
          }
        } else {
          strlcpy(st.weather_error, e.weather.error[0] ? e.weather.error : "unavailable", sizeof(st.weather_error));
        }
        ESP_LOGI(TAG, "weather %s", e.weather.ok ? "ok" : st.weather_error);
        ui::on_weather();
        break;
      }
      case EventType::PlacesDone:
        ui::on_places();
        break;
      case EventType::CryptoData:
        ui::on_crypto();
        break;
      case EventType::CryptoChart:
        ui::on_crypto_chart();
        break;
      case EventType::CommandResult:
        ESP_LOGI(TAG, "<- #%u %s status=%d%s", static_cast<unsigned>(e.cmd.request_id), e.cmd.action,
                 static_cast<int>(e.cmd.status), e.cmd.simulated ? " (simulated)" : "");
        ui::on_command_result(e.cmd);
        break;
    }
  }
  if (telemetry) {
    evaluate_link(now);
    ui::on_telemetry();
  }
}

// Auto sleep after N minutes without a touch: deep sleep on the decks that wake from it
// (PCD_HAS_DEEP_SLEEP), else the screen turns off (a touch or BOOT turns it back on). A toast warns
// 10 s before (screen on only); any touch restarts the countdown. LVGL's inactivity timer is reset by
// every press. While the screensaver is enabled it replaces auto sleep: it starts after its own delay
// instead, and the deck never sleeps by itself.
void check_auto_sleep() {
  const storage::Screensaver &ss = state().settings.ss;
  static bool warned = false;
  if (ss.enabled) {
    warned = false;
    if (!ui::screensaver::active() && !power::is_screen_off() && !ui::setup::active() &&
        lv_disp_get_inactive_time(nullptr) >= static_cast<uint32_t>(ss.wait_min) * 60000u) {
      ESP_LOGI(TAG, "screensaver after %u min idle", ss.wait_min);
      ui::screensaver::start();
    }
    return;
  }
  const uint8_t min = state().settings.auto_sleep_min;
  if (min == 0) {
    warned = false;
    return;
  }
  const uint32_t idle = lv_disp_get_inactive_time(nullptr);
  const uint32_t limit = static_cast<uint32_t>(min) * 60000u;
  constexpr uint32_t kWarnMs = 10000;
  if (idle >= limit) {
    warned = false;
#if PCD_HAS_DEEP_SLEEP
    ESP_LOGI("app", "auto deep sleep after %u min idle", min);
    power::deep_sleep();
#else
    if (!power::is_screen_off()) {
      ESP_LOGI("app", "auto sleep (screen off) after %u min idle", min);
      power::screen_off();
    }
#endif
    return;
  }
  if (idle + kWarnMs >= limit) {
    if (!warned && !power::is_screen_off()) ui::toast(ui::tr(ui::Str::AutoSleepSoon), ui::theme::kGpu);
    warned = true;
  } else {
    warned = false;
  }
}

void log_diag() {
  const auto &ps = lvgl_port::stats();
  ESP_LOGI(TAG, "diag heap_int=%u min=%u largest=%u psram=%u frames=%u push=%uus rx=%u dropped=%u link=%d",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), static_cast<unsigned>(ps.frames),
           static_cast<unsigned>(ps.last_push_us), static_cast<unsigned>(state().rx_count),
           static_cast<unsigned>(bus::dropped()), static_cast<int>(state().link));
}

}  // namespace

void setup() {
  con::begin();  // logs and the command console on the deck's USB port
  delay(300);
  ESP_LOGI(TAG, "DECKY firmware %s (%s), reset reason %s", PCD_FW_VERSION, PCD_DECK_KEY, reset_reason());
  ESP_LOGI(TAG, "PSRAM %u KB free, internal %u KB free",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

  AppState &st = state();
  if (!storage::load(st.settings)) ESP_LOGI(TAG, "settings: defaults (first boot or schema change)");
  ui::set_lang(st.settings.lang);
  apply_alert_thresholds();
  apply_capabilities();

  power::init();  // before backlight: releases the pin held low during deep sleep
  backlight::init(0);
  battery::init();
  if (!lvgl_port::init()) {
    ESP_LOGE(TAG, "display init failed, halting");
    backlight::set(100);
    while (true) delay(1000);
  }
  bus::init(6);
  commands::init();
  ui::theme::init();
  // PC pages at boot: none until the PC app connects (update_pc_presence adds them), demo mode aside.
  st.pc_present = st.settings.demo_mode;
  ui::build();
  {
    // Nothing configured yet (first power-on, or the network was forgotten): setup assistant.
    storage::NetConfig nc;
    storage::load_net(nc);
    const bool has_wifi = nc.has_wifi();
    memset(&nc, 0, sizeof(nc));
    if (!has_wifi && !st.settings.demo_mode) ui::setup::open();
  }
  lv_timer_handler();  // first frame before the backlight comes on
  s_setup_lit = ui::setup::active();
  backlight::set(screen_brightness());

  // The network task owns Wi-Fi; in demo mode it never connects (nothing leaves the device). An S3's
  // radio stays off until then; the P4's ESP32-C6 is brought up once by the network task and kept in
  // STA mode (PCD_WIFI_ESP_HOSTED).
#if !PCD_WIFI_ESP_HOSTED
  WiFi.mode(WIFI_OFF);
#endif
  net::start();
  net::set_enabled(!st.settings.demo_mode);
  net::set_weather_place(st.settings.place_name[0] != '\0', st.settings.place_lat, st.settings.place_lon);
  crypto_sync();

  if (st.settings.demo_mode) demo::start();
  evaluate_link(millis());
  log_diag();
}

// The PC pages follow the PC: shown while its app is connected (or in demo mode), hidden otherwise —
// from power-on too, so they appear when the PC does instead of vanishing when it does not.
static void update_pc_presence() {
  AppState &st = state();
  const bool present = st.settings.demo_mode || st.link == LinkState::Online || st.link == LinkState::Stale;
  if (present == st.pc_present) return;
  st.pc_present = present;
  ESP_LOGI(TAG, "PC %s: PC pages %s", present ? "connected" : "not connected", present ? "shown" : "hidden");
  ui::on_pc_presence();
}

void loop() {
  const uint32_t now = millis();
  console::poll();
  drain_events(now);
  commands::check_timeouts(now);

  if (now - s_last_1s >= 1000) {
    s_last_1s = now;
    evaluate_link(now);
    update_pc_presence();
    ui::tick_1s();
    check_auto_sleep();
  }
  if (now - s_last_diag >= 10000) {
    s_last_diag = now;
    log_diag();
  }
  if (s_rebuild_pending) {
    // Rebuild outside of the LVGL event that requested it (deleting the caller mid-event is unsafe).
    s_rebuild_pending = false;
    ui::build();
  }

  battery::poll(now);
  power::poll(now);
  lvgl_port::handle();
  // The setup assistant opens (first power-on, the status LED...) and closes from the UI: the
  // backlight follows it at once. While the screen is off, waking up applies it.
  if (ui::setup::active() != s_setup_lit) {
    s_setup_lit = !s_setup_lit;
    if (!power::is_screen_off() && !ui::screensaver::active()) backlight::set(screen_brightness());
  }
  delay(5);
}

void set_brightness(uint8_t percent, bool persist) {
  AppState &st = state();
  st.settings.brightness = percent;
  backlight::set(percent);
  if (persist) storage::save(st.settings);
}

// The setup assistant runs at full brightness: a first power-on is often in a bright room, and
// the user hasn't chosen a level yet. Their own level comes back when it closes.
uint8_t screen_brightness() { return ui::setup::active() ? 100 : state().settings.brightness; }

void set_language(storage::Lang lang) {
  AppState &st = state();
  if (st.settings.lang == lang) return;
  st.settings.lang = lang;
  ui::set_lang(lang);
  storage::save(st.settings);
  s_rebuild_pending = true;
}

void set_alerts_enabled(bool on) {
  AppState &st = state();
  st.settings.alerts_enabled = on;
  storage::save(st.settings);
}

void set_refresh(uint8_t seconds) {
  if (seconds != 1 && seconds != 2 && seconds != 5) return;
  AppState &st = state();
  st.settings.refresh_s = seconds;
  storage::save(st.settings);
}

void set_place(const char *name, float lat, float lon) {
  AppState &st = state();
  strlcpy(st.settings.place_name, name, sizeof(st.settings.place_name));
  st.settings.place_lat = lat;
  st.settings.place_lon = lon;
  storage::save(st.settings);
  st.weather = telemetry::Weather{};  // the old place's forecast must not show under the new name
  st.weather_error[0] = '\0';
  net::set_weather_place(name[0] != '\0', lat, lon);
  ui::on_weather();
}

void set_temp_f(bool f) {
  AppState &st = state();
  st.settings.temp_f = f;
  storage::save(st.settings);
  ui::on_weather();
}

void set_clock_12h(bool twelve) {
  AppState &st = state();
  st.settings.clock_12h = twelve;
  storage::save(st.settings);
  ui::on_weather();
}

void set_today_bg(bool on) {
  AppState &st = state();
  st.settings.today_bg = on;
  storage::save(st.settings);
  ui::on_weather();
}

void set_auto_sleep(uint8_t minutes) {
  if (!storage::valid_auto_sleep(minutes)) return;
  AppState &st = state();
  st.settings.auto_sleep_min = minutes;
  storage::save(st.settings);
  lv_disp_trig_activity(nullptr);  // the countdown starts now, not from the last touch
}

void set_screensaver(const storage::Screensaver &ss) {
  if (!storage::valid_screensaver(ss)) return;
  AppState &st = state();
  const bool was_on = st.settings.ss.enabled;
  st.settings.ss = ss;
  if (st.settings.ss.bg >= ui::wallpaper::count()) st.settings.ss.bg = 0;
  storage::save(st.settings);
  if (ss.enabled != was_on) lv_disp_trig_activity(nullptr);  // the countdown starts now
  if (!ss.enabled) ui::screensaver::stop();
}

void set_tab_hidden(uint8_t tab, bool hidden) {
  if (tab >= storage::kTabCount) return;
  AppState &st = state();
  const uint8_t bit = static_cast<uint8_t>(1u << tab);
  st.settings.tab_hidden = hidden ? (st.settings.tab_hidden | bit) : (st.settings.tab_hidden & ~bit);
  storage::save(st.settings);
  ui::apply_tab_order();
}

void set_tab_order(const uint8_t order[storage::kTabCount]) {
  if (!storage::valid_tab_order(order)) return;
  AppState &st = state();
  memcpy(st.settings.tab_order, order, sizeof(st.settings.tab_order));
  storage::save(st.settings);
  ui::apply_tab_order();
}

void set_demo_mode(bool on) {
  AppState &st = state();
  if (st.settings.demo_mode == on) return;
  st.settings.demo_mode = on;
  storage::save(st.settings);
  if (on) {
    net::set_enabled(false);
    demo::set_paused(false);
    demo::start();
  } else {
    demo::stop();
    net::set_enabled(true);
  }
  // Synthetic history must never mix with real data: wipe it on every mode switch.
  clear_history();
  st.last_rx_ms = 0;
  st.agent_note_count = 0;
  st.agent_first_note[0] = '\0';
  st.agent_provider[0] = '\0';
  apply_capabilities();
  evaluate_link(millis());
  ui::on_link();
  ui::on_telemetry();
  crypto_sync();
}

void set_demo_paused(bool paused) { demo::set_paused(paused); }

void set_button_action(uint8_t action) {
  AppState &st = state();
  st.settings.button_action = action == 1 ? 1 : 0;
  storage::save(st.settings);
}

void crypto_sync() {
  const storage::Settings &cfg = state().settings;
  int8_t idx[crypto::kMaxCoins];
  const int n = crypto::parse_watch(cfg.crypto_coins, idx, crypto::kMaxCoins);
  const ui::Page p = ui::current_page();
  const bool visible = p == ui::Page::Crypto || p == ui::Page::CryptoDetail;
  const bool enabled = !(cfg.tab_hidden & (1u << static_cast<uint8_t>(storage::Tab::Crypto)));
  const uint32_t period = visible ? cfg.crypto_refresh_s * 1000u : 120000u;
  net::set_crypto(idx, n, ui::cfmt::currency(cfg.crypto_cur).code, period, (enabled || visible) && !cfg.demo_mode);
}

void set_crypto_coins(const char *csv) {
  AppState &st = state();
  int8_t idx[crypto::kMaxCoins];
  const int n = crypto::parse_watch(csv, idx, crypto::kMaxCoins);
  crypto::format_watch(idx, n, st.settings.crypto_coins, sizeof(st.settings.crypto_coins));
  storage::save(st.settings);
  crypto_sync();
  ui::on_crypto_watch();
}

void set_crypto_currency(uint8_t cur) {
  if (cur >= storage::kCurrencyCount) return;
  AppState &st = state();
  if (st.settings.crypto_cur == cur) return;
  st.settings.crypto_cur = cur;
  storage::save(st.settings);
  crypto::clear_charts();  // charts are cached per currency; drop the old ones
  crypto_sync();
  ui::on_crypto_watch();
}

void set_crypto_refresh(uint8_t seconds) {
  if (!storage::valid_crypto_refresh(seconds)) return;
  AppState &st = state();
  st.settings.crypto_refresh_s = seconds;
  storage::save(st.settings);
  crypto_sync();
}

void set_crypto_style(uint8_t style) {
  AppState &st = state();
  st.settings.crypto_style = style == 1 ? 1 : 0;
  storage::save(st.settings);
}

void set_crypto_range(uint8_t range) {
  if (range >= storage::kCryptoRanges) return;
  AppState &st = state();
  if (st.settings.crypto_range == range) return;
  st.settings.crypto_range = range;
  storage::save(st.settings);
}

const char *reset_reason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    default: return "UNKNOWN";
  }
}

}  // namespace app
