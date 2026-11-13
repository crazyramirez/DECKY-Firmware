#include "settings_store.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

namespace storage {
namespace {
const char *kNs = "pcd";
constexpr uint8_t kSchema = 1;
}  // namespace

bool valid_auto_sleep(uint8_t m) { return m == 0 || m == 1 || m == 2 || m == 5 || m == 10; }
bool valid_crypto_refresh(uint8_t s) { return s == 15 || s == 30 || s == 60; }

bool valid_screensaver(const Screensaver &ss) {
  const uint8_t w = ss.wait_min, c = ss.cycle_min, d = ss.dim;
  return (w == 1 || w == 2 || w == 5 || w == 10 || w == 30) && ss.bg_mode <= 2 &&
         (c == 1 || c == 5 || c == 15 || c == 30) && ss.show <= 0x0F && (d == 100 || d == 60 || d == 30);
}

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
  Preferences p;
  if (!p.begin(kNs, true)) {
    // Namespace does not exist yet (first boot): defaults; create it so later opens are quiet.
    if (p.begin(kNs, false)) p.end();
    return false;
  }
  if (p.getUChar("schema", 0) != kSchema) {
    p.end();
    return false;
  }
  out.brightness = p.getUChar("bright", out.brightness);
  out.lang = static_cast<Lang>(p.getUChar("lang", static_cast<uint8_t>(out.lang)));
  out.alerts_enabled = p.getBool("alerts", out.alerts_enabled);
  out.demo_mode = false;  // demo mode is not a user feature: always boot with real data
  out.refresh_s = p.getUChar("refresh", out.refresh_s);
  out.auto_sleep_min = p.getUChar("asleep", out.auto_sleep_min);
  out.button_action = p.getUChar("btnact", 0) == 1 ? 1 : 0;
  uint8_t tabs[kTabCount];
  size_t got = p.getBytes("tabs", tabs, sizeof(tabs));
  // Saved by an older firmware with fewer tabs: append the new ones in their default order.
  for (size_t t = got; got >= 3 && t < static_cast<size_t>(kTabCount); t++) tabs[t] = static_cast<uint8_t>(t);
  if (got >= 3 && valid_tab_order(tabs)) memcpy(out.tab_order, tabs, sizeof(tabs));
  out.tab_hidden = p.getUChar("tabhide", 0) & ((1u << kTabCount) - 1);
  p.getString("devname", out.device_name, sizeof(out.device_name));
  if (out.device_name[0] == '\0') strlcpy(out.device_name, "PC", sizeof(out.device_name));
  p.getString("place", out.place_name, sizeof(out.place_name));
  out.place_lat = p.getFloat("plat", 0.0f);
  out.place_lon = p.getFloat("plon", 0.0f);
  if (!isfinite(out.place_lat) || !isfinite(out.place_lon) || fabsf(out.place_lat) > 90 || fabsf(out.place_lon) > 180)
    out.place_name[0] = '\0';
  out.temp_f = p.getBool("tempf", false);
  out.clock_12h = p.getBool("clk12", false);
  out.today_bg = p.getBool("todaybg", true);
  out.utc_offset_min = p.getShort("utcoff", 0);
  if (p.isKey("ccoins")) p.getString("ccoins", out.crypto_coins, sizeof(out.crypto_coins));
  out.crypto_cur = p.getUChar("ccur", out.crypto_cur);
  out.crypto_refresh_s = p.getUChar("crefr", out.crypto_refresh_s);
  out.crypto_style = p.getUChar("cstyle", out.crypto_style);
  out.crypto_range = p.getUChar("crange", out.crypto_range);
  Screensaver ss;
  ss.enabled = p.getBool("ss_on", ss.enabled);
  ss.wait_min = p.getUChar("ss_wait", ss.wait_min);
  ss.bg_mode = p.getUChar("ss_mode", ss.bg_mode);
  ss.bg = p.getUChar("ss_bg", ss.bg);
  ss.cycle_min = p.getUChar("ss_cycle", ss.cycle_min);
  ss.show = p.getUChar("ss_show", ss.show);
  ss.dim = p.getUChar("ss_dim", ss.dim);
  if (valid_screensaver(ss)) out.ss = ss;
  out.cpu_warn_c = p.getFloat("cpu_w", out.cpu_warn_c);
  out.cpu_crit_c = p.getFloat("cpu_c", out.cpu_crit_c);
  out.gpu_warn_c = p.getFloat("gpu_w", out.gpu_warn_c);
  out.gpu_crit_c = p.getFloat("gpu_c", out.gpu_crit_c);
  p.end();

  if (out.brightness < 5 || out.brightness > 100) out.brightness = 80;
  if (out.lang != Lang::Es && out.lang != Lang::En) out.lang = Lang::Es;
  if (out.refresh_s != 1 && out.refresh_s != 2 && out.refresh_s != 5) out.refresh_s = 5;
  if (!valid_auto_sleep(out.auto_sleep_min)) out.auto_sleep_min = 0;
  if (out.crypto_cur >= kCurrencyCount) out.crypto_cur = static_cast<uint8_t>(Currency::Eur);
  if (!valid_crypto_refresh(out.crypto_refresh_s)) out.crypto_refresh_s = 30;
  if (out.crypto_style > 1) out.crypto_style = 0;
  if (out.crypto_range >= kCryptoRanges) out.crypto_range = 1;
  return true;
}

bool save(const Settings &s) {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  p.putUChar("schema", kSchema);
  p.putUChar("bright", s.brightness);
  p.putUChar("lang", static_cast<uint8_t>(s.lang));
  p.putBool("alerts", s.alerts_enabled);
  p.putBool("demo", s.demo_mode);
  p.putBytes("tabs", s.tab_order, sizeof(s.tab_order));
  p.putUChar("refresh", s.refresh_s);
  p.putUChar("tabhide", s.tab_hidden);
  p.putUChar("asleep", s.auto_sleep_min);
  p.putUChar("btnact", s.button_action);
  p.putString("devname", s.device_name);
  p.putString("place", s.place_name);
  p.putFloat("plat", s.place_lat);
  p.putFloat("plon", s.place_lon);
  p.putBool("tempf", s.temp_f);
  p.putBool("clk12", s.clock_12h);
  p.putBool("todaybg", s.today_bg);
  p.putShort("utcoff", s.utc_offset_min);
  p.putString("ccoins", s.crypto_coins);
  p.putUChar("ccur", s.crypto_cur);
  p.putUChar("crefr", s.crypto_refresh_s);
  p.putUChar("cstyle", s.crypto_style);
  p.putUChar("crange", s.crypto_range);
  p.putBool("ss_on", s.ss.enabled);
  p.putUChar("ss_wait", s.ss.wait_min);
  p.putUChar("ss_mode", s.ss.bg_mode);
  p.putUChar("ss_bg", s.ss.bg);
  p.putUChar("ss_cycle", s.ss.cycle_min);
  p.putUChar("ss_show", s.ss.show);
  p.putUChar("ss_dim", s.ss.dim);
  p.putFloat("cpu_w", s.cpu_warn_c);
  p.putFloat("cpu_c", s.cpu_crit_c);
  p.putFloat("gpu_w", s.gpu_warn_c);
  p.putFloat("gpu_c", s.gpu_crit_c);
  p.end();
  return true;
}

bool factory_reset() {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  const bool ok = p.clear();
  p.end();
  return ok;
}

}  // namespace storage
