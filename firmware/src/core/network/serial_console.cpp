#include "network/serial_console.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/power.h"
#include "app/state_store.h"
#include "board/battery.h"
#include "app/deck_hooks.h"
#include "board/console_io.h"
#include "board/lvgl_port.h"
#include "board_pins.h"
#include "deck_config.h"
#include "crypto/crypto_store.h"
#include "crypto/watchlist.h"
#include "network/net_task.h"
#include "storage/net_store.h"
#include "storage/settings_store.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"
#include "ui/wallpaper.h"

namespace console {
namespace {

constexpr size_t kMaxLine = 160;
char s_line[kMaxLine + 1];
size_t s_len = 0;
bool s_overflow = false;

void out(const char *s) { con::println(s); }

// Extract the first token (optionally "quoted") into dst; returns pointer to the rest (trimmed).
const char *take_token(const char *p, char *dst, size_t n, bool *ok) {
  while (*p == ' ') p++;
  size_t i = 0;
  *ok = true;
  if (*p == '"') {
    p++;
    while (*p && *p != '"') {
      if (i + 1 >= n) *ok = false;
      else dst[i++] = *p;
      p++;
    }
    if (*p != '"') *ok = false;
    else p++;
  } else {
    while (*p && *p != ' ') {
      if (i + 1 >= n) *ok = false;
      else dst[i++] = *p;
      p++;
    }
  }
  dst[i] = '\0';
  while (*p == ' ') p++;
  return p;
}

void cmd_status() {
  storage::NetConfig c;
  storage::load_net(c);
  const net::Status s = net::status();
  const auto &st = app::state();
  con::printf("fw %s | demo %s | link %d | uptime %lus | reset %s | heap %u\n", PCD_FW_VERSION,
                st.settings.demo_mode ? "on" : "off", static_cast<int>(st.link),
                static_cast<unsigned long>(millis() / 1000), app::reset_reason(),
                static_cast<unsigned>(ESP.getFreeHeap()));
  con::printf("wifi ssid='%s' password=%s connected=%d ip=%s rssi=%d\n", c.ssid, c.pass[0] ? "<set>" : "<empty>",
                s.wifi_connected, s.ip[0] ? s.ip : "-", s.rssi);
  con::printf("agent %s:%u ws=%d rx=%u rejected=%u reconnects=%u backoff=%ums\n", c.host[0] ? c.host : "-", c.port,
                s.ws_connected, static_cast<unsigned>(s.rx_frames), static_cast<unsigned>(s.rx_rejected),
                static_cast<unsigned>(s.reconnects), static_cast<unsigned>(s.backoff_ms));
  const battery::State b = battery::state();
  con::printf("deck power %s node=%umV pct=%d (adc pin %u mV)\n", battery::source_name(b.source), b.mv, b.pct, b.pin_mv);
  deck::console_status();
  if (st.agent_note_count)
    con::printf("agent notes (%u): %s\n", st.agent_note_count, st.agent_first_note);
}

// Dump the current frame as base64 (RGB565 as the panel takes it) between SHOT markers. Developer aid to
// review the UI remotely; tools/screenshot.py turns it into a PNG. Runs in the UI task, so the
// frame is not being rendered while we read it.
void cmd_shot() {
  static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t *p = reinterpret_cast<const uint8_t *>(lvgl_port::framebuffer());
  if (!p) {
    out("ERR no framebuffer");
    return;
  }
  const size_t n = PCD_LCD_H_RES * PCD_LCD_V_RES * 2;
  con::printf("SHOT BEGIN %d %d %s\n", PCD_LCD_H_RES, PCD_LCD_V_RES, LV_COLOR_16_SWAP ? "rgb565be" : "rgb565le");
  char line[98];  // '#' + 72 input bytes -> 96 chars + newline
  size_t i = 0;
  while (i < n) {
    size_t k = 0;
    line[k++] = '#';  // marks frame data so interleaved log lines can be skipped
    for (size_t j = 0; j < 72 && i < n; j += 3, i += 3) {
      const uint32_t v = (uint32_t(p[i]) << 16) | (uint32_t(p[i + 1]) << 8) | p[i + 2];  // n % 3 == 0
      line[k++] = b64[(v >> 18) & 63];
      line[k++] = b64[(v >> 12) & 63];
      line[k++] = b64[(v >> 6) & 63];
      line[k++] = b64[v & 63];
    }
    line[k++] = '\n';
    if (con::write(line, k, 2000) != k) {  // host gone: stop instead of blocking for 1.6 MB
      out("");
      out("SHOT ABORT");
      return;
    }
  }
  out("SHOT END");
  con::flush();
}

void handle(char *line) {
  // trim
  while (*line == ' ' || *line == '\t') line++;
  size_t n = strlen(line);
  while (n && (line[n - 1] == ' ' || line[n - 1] == '\r' || line[n - 1] == '\t')) line[--n] = '\0';
  if (!n) return;

  char cmd[12];
  bool ok;
  const char *rest = take_token(line, cmd, sizeof(cmd), &ok);
  for (char *c = cmd; *c; c++) *c = static_cast<char>(tolower(*c));

  if (!strcmp(cmd, "help")) {
    out("commands: help | status | id | wifi <ssid> <password> | wifi reconnect | agent <host> [port] | discover | pair <ip> <code> [port]");
    out("          net clear | page <monitor|thermals|controls|settings|wifi|pair|cpu|gpu|today|crypto> | scroll <y>");
    out("          coin <SYM> | coins <BTC,ETH,...> | crypto | modal paircode|pairmanual|wifipass|place [q]|coins");
    out("          tabs <a> <b> <c> <d> <e> | tabhide <t> <0|1> | place <lat> <lon> <name> | weather | screen on|off | saver [now|off|bg <n>] | shot | demo on|off | reboot");
    out("          nopc 1|0   (ignore the paired PC until reboot: weather and crypto straight from the internet)");
    if (*deck::console_help()) out(deck::console_help());
  } else if (!strcmp(cmd, "status")) {
    cmd_status();
  } else if (!strcmp(cmd, "id")) {
    // Board model + firmware version: the PC app's USB updater identifies the deck with it.
    con::printf("pcdeck board=%s fw=%s\n", PCD_DECK_KEY, PCD_FW_VERSION);
  } else if (!strcmp(cmd, "wifi") && !strcmp(rest, "reconnect")) {
    // Developer aid: what "Connect" in Settings > Wi-Fi does, with the stored network.
    net::reconfigure();
    out("OK reconnecting to the stored network");
  } else if (!strcmp(cmd, "wifi")) {
    storage::NetConfig c;
    storage::load_net(c);
    char ssid[sizeof(c.ssid)];
    rest = take_token(rest, ssid, sizeof(ssid), &ok);
    const size_t plen = strlen(rest);
    if (!ok || !ssid[0]) {
      out("ERR usage: wifi <ssid> <password>   (SSID <= 32 bytes, quote it if it contains spaces)");
    } else if (plen > 64 || (plen > 0 && plen < 8)) {
      out("ERR password must be 8..64 characters (or empty for an open network)");
    } else {
      strlcpy(c.ssid, ssid, sizeof(c.ssid));
      strlcpy(c.pass, rest, sizeof(c.pass));
      storage::save_net(c);
      net::reconfigure();
      con::printf("OK wifi '%s' saved (password not shown)\n", c.ssid);
    }
  } else if (!strcmp(cmd, "agent")) {
    storage::NetConfig c;
    storage::load_net(c);
    char host[sizeof(c.host)];
    char port_s[8] = "";
    rest = take_token(rest, host, sizeof(host), &ok);
    bool ok2 = true;
    if (*rest) take_token(rest, port_s, sizeof(port_s), &ok2);
    const long port = port_s[0] ? strtol(port_s, nullptr, 10) : 8765;
    if (!ok || !ok2 || !host[0] || port < 1 || port > 65535) {
      out("ERR usage: agent <host> [port]");
    } else {
      strlcpy(c.host, host, sizeof(c.host));
      c.port = static_cast<uint16_t>(port);
      storage::save_net(c);
      net::reconfigure();
      con::printf("OK agent %s:%u saved\n", c.host, c.port);
    }
  } else if (!strcmp(cmd, "demo")) {
    if (!strcmp(rest, "on") || !strcmp(rest, "off")) {
      app::set_demo_mode(!strcmp(rest, "on"));
      con::printf("OK demo %s\n", rest);
    } else {
      out("ERR usage: demo on|off");
    }
  } else if (!strcmp(cmd, "net") && !strcmp(rest, "clear")) {
    storage::clear_net();
    net::reconfigure();
    out("OK network credentials erased");
  } else if (!strcmp(cmd, "shot")) {
    cmd_shot();
  } else if (!strcmp(cmd, "scroll")) {
    ui::dev_scroll(static_cast<int>(strtol(rest, nullptr, 10)));
    out("OK");
  } else if (!strcmp(cmd, "discover")) {
    net::request_discovery();
    out("OK discovery requested (results in the log)");
  } else if (!strcmp(cmd, "pair")) {
    // pair <ipv4> <6-digit code> [port]
    net::PcEntry pc;
    char code[8] = "", port_s[8] = "";
    bool ok1 = true, ok2 = true, ok3 = true;
    rest = take_token(rest, pc.host, sizeof(pc.host), &ok1);
    rest = take_token(rest, code, sizeof(code), &ok2);
    if (*rest) take_token(rest, port_s, sizeof(port_s), &ok3);
    pc.port = port_s[0] ? static_cast<uint16_t>(strtol(port_s, nullptr, 10)) : 8765;
    strlcpy(pc.name, pc.host, sizeof(pc.name));
    if (!ok1 || !ok2 || !ok3 || !pc.host[0] || strlen(code) != 6) {
      out("ERR usage: pair <ipv4> <6-digit code> [port]");
    } else {
      if (app::state().settings.demo_mode) app::set_demo_mode(false);
      net::start_pairing(pc, code);
      out("OK pairing started (result in the log)");
    }
  } else if (!strcmp(cmd, "modal")) {
    // modal paircode|pairmanual|wifipass|place [query]  (developer aid for screenshots; Back/X closes it)
    if (!strcmp(rest, "paircode")) ui::pair::dev_open_code();
    else if (!strcmp(rest, "pairmanual")) ui::pair::dev_open_manual();
    else if (!strcmp(rest, "wifipass")) ui::wifi::dev_open_connect("MyNetwork");
    else if (!strncmp(rest, "place", 5)) ui::place::dev_search(rest[5] == ' ' ? rest + 6 : "");
    else if (!strcmp(rest, "coins")) ui::coins::open();
    out("OK");
  } else if (!strcmp(cmd, "page")) {
    static const struct {
      const char *name;
      ui::Page page;
    } kPages[] = {{"home", ui::Page::Home},         {"monitor", ui::Page::Home},         {"thermals", ui::Page::Thermals}, {"controls", ui::Page::Controls},
                  {"settings", ui::Page::Settings}, {"wifi", ui::Page::Wifi},         {"cpu", ui::Page::DetailCpu},
                  {"gpu", ui::Page::DetailGpu},     {"pair", ui::Page::Pair},           {"today", ui::Page::Today},
                  {"crypto", ui::Page::Crypto}};
    bool found = false;
    for (const auto &p : kPages) {
      if (!strcmp(rest, p.name)) {
        ui::navigate(p.page);
        found = true;
      }
    }
    out(found ? "OK" : "ERR usage: page home|thermals|controls|settings|wifi|cpu|gpu|today|crypto");
  } else if (!strcmp(cmd, "coin")) {
    const int idx = crypto::catalog_index(rest);
    if (idx < 0) {
      out("ERR usage: coin <SYM>  (catalog symbol, e.g. BTC)");
    } else {
      ui::crypto_detail::show(idx);
      out("OK");
    }
  } else if (!strcmp(cmd, "coins")) {
    app::set_crypto_coins(rest);
    con::printf("OK coins '%s'\n", app::state().settings.crypto_coins);
  } else if (!strcmp(cmd, "crypto")) {
    static crypto::Market m;  // ~1.7 KB: keep it off the loop task stack
    uint32_t rx = 0;
    char err[24];
    crypto::market_error(err, sizeof(err));
    const bool have = crypto::get_market(m, &rx);
    con::printf("watch '%s' cur=%u refresh=%us | data=%d age=%us cur=%s coins=%u fng=%d err='%s'\n",
                  app::state().settings.crypto_coins, app::state().settings.crypto_cur,
                  app::state().settings.crypto_refresh_s, have, have ? static_cast<unsigned>((millis() - rx) / 1000) : 0,
                  have ? m.cur : "-", have ? m.count : 0, have ? m.fng : -1, err);
    for (int i = 0; have && i < m.count; i++)
      con::printf("  %-12s %.8g  %+.2f%%  spark=%u\n", m.coins[i].pair, m.coins[i].price,
                    static_cast<double>(m.coins[i].chg_pct), m.coins[i].spark_n);
  } else if (!strcmp(cmd, "tabs")) {
    // tabs <a> <b> <c> <d> <e>  (0 monitor, 1 thermals, 2 player, 3 today, 4 crypto), same as Settings > Tabs
    uint8_t order[storage::kTabCount];
    int n = sscanf(rest, "%hhu %hhu %hhu %hhu %hhu", &order[0], &order[1], &order[2], &order[3], &order[4]);
    if (n != storage::kTabCount || !storage::valid_tab_order(order)) {
      out("ERR usage: tabs <a> <b> <c> <d> <e>  (permutation of 0 monitor, 1 thermals, 2 player, 3 today, 4 crypto)");
    } else {
      app::set_tab_order(order);
      out("OK");
    }
  } else if (!strcmp(cmd, "place")) {
    // place <lat> <lon> <name...> | place off   (same as picking a city in the search)
    float lat = 0, lon = 0;
    int used = 0;
    if (!strcmp(rest, "off")) {
      app::set_place("", 0, 0);
      out("OK");
    } else if (sscanf(rest, "%f %f %n", &lat, &lon, &used) == 2 && used > 0 && rest[used]) {
      app::set_place(rest + used, lat, lon);
      out("OK");
    } else {
      out("ERR usage: place <lat> <lon> <name> | place off");
    }
  } else if (!strcmp(cmd, "weather")) {
    const auto &st = app::state();
    const auto &w = st.weather;
    con::printf("place '%s' %.4f,%.4f | ok=%d err='%s' code=%d t=%.1f day=%d hours=%u days=%u\n",
                  st.settings.place_name, st.settings.place_lat, st.settings.place_lon, w.ok, st.weather_error, w.code,
                  w.temp, w.day, w.hour_count, w.day_count);
    struct tm t;
    if (app::local_time(t))
      con::printf("clock %04d-%02d-%02d %02d:%02d:%02d (UTC%+d min)\n", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec, st.settings.utc_offset_min);
    else
      out("clock not set");
  } else if (!strcmp(cmd, "nopc")) {
    if (!strcmp(rest, "1") || !strcmp(rest, "0")) {
      net::set_agent_paused(!strcmp(rest, "1"));
      out("OK");
    } else {
      out("ERR usage: nopc 1|0");
    }
  } else if (!strcmp(cmd, "tabhide")) {
    // tabhide <tab> <0|1>  (0 monitor, 1 thermals, 2 player, 3 today, 4 crypto)
    unsigned t = 9, h = 9;
    if (sscanf(rest, "%u %u", &t, &h) != 2 || t >= storage::kTabCount || h > 1) {
      out("ERR usage: tabhide <0 monitor|1 thermals|2 player|3 today|4 crypto> <0 show|1 hide>");
    } else {
      app::set_tab_hidden(static_cast<uint8_t>(t), h == 1);
      out("OK");
    }
  } else if (!strcmp(cmd, "screen")) {
    if (!strcmp(rest, "off")) power::screen_off();
    else if (!strcmp(rest, "on")) power::screen_on();
    out(!strcmp(rest, "off") || !strcmp(rest, "on") ? "OK" : "ERR usage: screen on|off");
  } else if (!strcmp(cmd, "saver")) {
    // saver [now|off|bg <n>]: show / close the screensaver, pick its wallpaper (developer aid)
    unsigned n = 0;
    if (!strcmp(rest, "off")) {
      ui::screensaver::stop();
      out("OK");
    } else if (sscanf(rest, "bg %u", &n) == 1 && n < static_cast<unsigned>(ui::wallpaper::count())) {
      storage::Screensaver ss = app::state().settings.ss;
      ss.bg = static_cast<uint8_t>(n);
      app::set_screensaver(ss);
      ui::settings::on_screensaver();
      out("OK");
    } else if (!rest[0] || !strcmp(rest, "now")) {
      ui::screensaver::start();
      const auto &st = app::state();
      con::printf("OK saver on=%d wait=%u mode=%u bg=%u show=0x%02X dim=%u | snapshot=%d link=%d\n",
                  st.settings.ss.enabled, st.settings.ss.wait_min, st.settings.ss.bg_mode, st.settings.ss.bg,
                  st.settings.ss.show, st.settings.ss.dim, st.has_snapshot, static_cast<int>(st.link));
    } else {
      out("ERR usage: saver [now|off|bg <0..7>]");
    }
  } else if (!strcmp(cmd, "reboot")) {
    out("rebooting...");
    con::flush();
    delay(100);
    ESP.restart();
  } else if (deck::console_command(cmd, rest)) {
    // answered by this deck's own commands
  } else {
    out("ERR unknown command (type 'help')");
  }
}

}  // namespace

void poll() {
  uint8_t buf[64];
  const int got = con::read(buf, sizeof(buf));
  for (int k = 0; k < got; k++) {
    const int ch = buf[k];
    if (ch == '\n') {
      s_line[s_len] = '\0';
      if (s_overflow) out("ERR line too long");
      else handle(s_line);
      s_len = 0;
      s_overflow = false;
    } else if (s_len < kMaxLine) {
      s_line[s_len++] = static_cast<char>(ch);
    } else {
      s_overflow = true;
    }
  }
}

}  // namespace console
