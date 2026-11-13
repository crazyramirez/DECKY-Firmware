// Link stubs for the desktop UI simulator: everything the UI calls outside ui/.
#include <stdio.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/power.h"
#include "app/state_store.h"
#include "board/backlight.h"
#include "board/battery.h"
#include "commands/command_client.h"
#include "crypto/watchlist.h"
#include "network/net_task.h"
#include "storage/net_store.h"
#include "storage/settings_store.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace app {
void set_brightness(uint8_t p, bool) { state().settings.brightness = p; }
void set_language(storage::Lang l) { state().settings.lang = l; }
void set_alerts_enabled(bool on) { state().settings.alerts_enabled = on; }
void set_refresh(uint8_t s) { state().settings.refresh_s = s; }
void set_auto_sleep(uint8_t m) { state().settings.auto_sleep_min = m; }
void set_tab_order(const uint8_t order[storage::kTabCount]) {
  memcpy(state().settings.tab_order, order, storage::kTabCount);
  ui::apply_tab_order();
}
void set_tab_hidden(uint8_t tab, bool hidden) {
  auto &h = state().settings.tab_hidden;
  h = hidden ? (h | (1u << tab)) : (h & ~(1u << tab));
  ui::apply_tab_order();
}
void set_place(const char *name, float lat, float lon) {
  strlcpy(state().settings.place_name, name, sizeof(state().settings.place_name));
  state().settings.place_lat = lat;
  state().settings.place_lon = lon;
}
void set_temp_f(bool f) { state().settings.temp_f = f; }
void set_clock_12h(bool t) { state().settings.clock_12h = t; }
void set_today_bg(bool on) { state().settings.today_bg = on; }
void set_button_action(uint8_t a) { state().settings.button_action = a; }
void set_screensaver(const storage::Screensaver &ss) { state().settings.ss = ss; }
void set_crypto_coins(const char *csv) {
  strlcpy(state().settings.crypto_coins, csv, sizeof(state().settings.crypto_coins));
  ui::on_crypto_watch();
}
void set_crypto_currency(uint8_t c) { state().settings.crypto_cur = c; }
void set_crypto_refresh(uint8_t s) { state().settings.crypto_refresh_s = s; }
void set_crypto_style(uint8_t s) { state().settings.crypto_style = s; }
void set_crypto_range(uint8_t r) { state().settings.crypto_range = r; }
void crypto_sync() {}
void set_demo_mode(bool on) { state().settings.demo_mode = on; }
void set_demo_paused(bool) {}
const char *reset_reason() { return "SIM"; }
}  // namespace app

namespace storage {
bool valid_auto_sleep(uint8_t m) { return m == 0 || m == 1 || m == 2 || m == 5 || m == 10; }
bool valid_crypto_refresh(uint8_t s) { return s == 15 || s == 30 || s == 60; }
bool valid_tab_order(const uint8_t order[kTabCount]) {
  bool seen[kTabCount] = {};
  for (int i = 0; i < kTabCount; i++) {
    if (order[i] >= kTabCount || seen[order[i]]) return false;
    seen[order[i]] = true;
  }
  return true;
}
bool load(Settings &out) {
  out = Settings{};
  return true;
}
bool save(const Settings &) { return true; }
bool factory_reset() { return true; }
NetConfig g_net;
bool load_net(NetConfig &out) {
  out = g_net;
  return true;
}
bool save_net(const NetConfig &c) {
  g_net = c;
  return true;
}
bool clear_net() {
  g_net = NetConfig{};
  return true;
}
}  // namespace storage

namespace net {
Status g_status;
ScanEntry g_scan[4];
int g_scan_n = 0;
PcEntry g_pcs[2];
int g_pc_n = 0;
bool start() { return true; }
void set_enabled(bool) {}
void reconfigure() {}
Status status() { return g_status; }
void request_scan() {}
int scan_results(ScanEntry *out, int max) {
  const int n = g_scan_n < max ? g_scan_n : max;
  memcpy(out, g_scan, sizeof(ScanEntry) * n);
  return n;
}
Failure classify(uint8_t) { return Failure::None; }
void request_discovery() {}
int discovery_results(PcEntry *out, int max) {
  const int n = g_pc_n < max ? g_pc_n : max;
  memcpy(out, g_pcs, sizeof(PcEntry) * n);
  return n;
}
void start_pairing(const PcEntry &, const char *) {}
void forget_pc() {}
void set_weather_place(bool, float, float) {}
void request_weather() {}
void search_places(const char *, const char *) {}
int place_results(telemetry::Place *, int) { return 0; }
void set_crypto(const int8_t *, int, const char *, uint32_t, bool) {}
void request_crypto() {}
void request_crypto_chart(int, uint8_t, const char *) {}
}  // namespace net

namespace commands {
bool init() { return true; }
const char *action_name(Action) { return "x"; }
uint32_t send(Action, int32_t) { return 1; }
bool next_outbound(Request &, uint32_t) { return false; }
bool resolve(uint32_t) { return true; }
void check_timeouts(uint32_t) {}
int pending_count() { return 0; }
}  // namespace commands

namespace battery {
State state() {
  State s;
  s.source = Source::Usb;
  return s;
}
const char *source_name(Source) { return "usb"; }
}  // namespace battery

namespace backlight {
void set(uint8_t) {}
uint8_t get() { return 80; }
}  // namespace backlight

namespace power {
void init() {}
void poll(uint32_t) {}
void screen_off() {}
void screen_on() {}
bool is_screen_off() { return false; }
void deep_sleep() {
  for (;;) {
  }
}
bool woke_from_deep_sleep() { return false; }
const char *wake_reason() { return ""; }
uint32_t touch_int_lows() { return 0; }
void button_info(int &a, int &b, uint32_t &t) { a = b = 0, t = 0; }
uint32_t boot_presses() { return 0; }
}  // namespace power
