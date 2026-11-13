#include "protocol_parser.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace proto {
namespace {

uint32_t s_boot_nonce = 0;

float num(JsonVariantConst v) {
  // is<float>() is true for any JSON number (integer or float); null/strings -> NaN.
  if (!v.is<float>()) return NAN;
  const float f = v.as<float>();
  return isfinite(f) ? f : NAN;
}

double dnum(JsonVariantConst v) {
  if (!v.is<double>()) return NAN;
  const double d = v.as<double>();
  return isfinite(d) ? d : NAN;
}

void str(JsonVariantConst v, char *dst, size_t n) {
  const char *s = v.is<const char *>() ? v.as<const char *>() : nullptr;
  if (!s) {
    dst[0] = '\0';
    return;
  }
  strlcpy(dst, s, n);
  // strlcpy may cut a UTF-8 sequence: drop a dangling partial character.
  size_t len = strlen(dst);
  size_t i = len;
  while (i > 0 && (static_cast<uint8_t>(dst[i - 1]) & 0xC0) == 0x80) i--;
  if (i > 0) {
    const uint8_t lead = static_cast<uint8_t>(dst[i - 1]);
    const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (len - (i - 1) < need) dst[i - 1] = '\0';
  }
}

void parse_snapshot(JsonObjectConst root, JsonObjectConst p, telemetry::Snapshot &s) {
  s = telemetry::Snapshot{};
  s.synthetic = false;
  str(root["deviceId"], s.host, sizeof(s.host));

  JsonObjectConst cpu = p["cpu"];
  str(cpu["name"], s.cpu.name, sizeof(s.cpu.name));
  str(cpu["tempSensorName"], s.cpu.temp_sensor_name, sizeof(s.cpu.temp_sensor_name));
  s.cpu.temp_c = num(cpu["tempC"]);
  s.cpu.core_max_temp_c = num(cpu["coreMaxTempC"]);
  s.cpu.load_pct = num(cpu["loadPct"]);
  s.cpu.freq_mhz = num(cpu["freqMHz"]);
  s.cpu.package_power_w = num(cpu["packagePowerW"]);

  JsonObjectConst gpu = p["gpu"];
  str(gpu["name"], s.gpu.name, sizeof(s.gpu.name));
  s.gpu.temp_c = num(gpu["tempC"]);
  s.gpu.load_pct = num(gpu["loadPct"]);
  s.gpu.vram_used_mib = num(gpu["vramUsedMiB"]);
  s.gpu.vram_total_mib = num(gpu["vramTotalMiB"]);
  s.gpu.board_power_w = num(gpu["boardPowerW"]);

  JsonObjectConst ram = p["ram"];
  s.ram.used_mib = num(ram["usedMiB"]);
  s.ram.total_mib = num(ram["totalMiB"]);

  s.storage_count = 0;
  for (JsonObjectConst d : p["storage"].as<JsonArrayConst>()) {
    if (s.storage_count >= telemetry::kMaxStorage) break;
    auto &dst = s.storage[s.storage_count++];
    str(d["name"], dst.name, sizeof(dst.name));
    dst.temp_c = num(d["tempC"]);
  }
  s.fan_count = 0;
  for (JsonObjectConst f : p["fans"].as<JsonArrayConst>()) {
    if (s.fan_count >= telemetry::kMaxFans) break;
    auto &dst = s.fans[s.fan_count++];
    str(f["label"], dst.label, sizeof(dst.label));
    dst.rpm = num(f["rpm"]);
  }
  JsonObjectConst audio = p["audio"];
  const float vol = num(audio["volumePct"]);
  s.volume_pct = telemetry::has(vol) && vol >= 0 && vol <= 100 ? static_cast<int8_t>(vol + 0.5f) : -1;
  s.muted = audio["muted"].as<bool>();
  JsonObjectConst bat = p["battery"];
  const float bpct = num(bat["pct"]);
  s.has_battery = !bat.isNull() && telemetry::has(bpct) && bpct >= 0 && bpct <= 100;
  if (s.has_battery) {
    auto tri = [](JsonVariantConst v) -> int8_t { return v.is<bool>() ? (v.as<bool>() ? 1 : 0) : -1; };
    s.bat_pct = static_cast<int8_t>(bpct + 0.5f);
    s.bat_plugged = tri(bat["plugged"]);
    s.bat_charging = tri(bat["charging"]);
    s.bat_secs_left = bat["secsLeft"].is<int>() && bat["secsLeft"].as<int>() >= 0 ? bat["secsLeft"].as<int>() : -1;
    s.bat_remaining_wh = num(bat["remainingWh"]);
    s.bat_full_wh = num(bat["fullWh"]);
    s.bat_rate_w = num(bat["rateW"]);
  }
  JsonObjectConst media = p["media"];
  s.has_media = !media.isNull();
  if (s.has_media) {
    str(media["title"], s.media_title, sizeof(s.media_title));
    str(media["artist"], s.media_artist, sizeof(s.media_artist));
    str(media["app"], s.media_app, sizeof(s.media_app));
    s.media_playing = media["playing"].as<bool>();
    s.media_pos_s = media["posS"].is<int>() ? media["posS"].as<int>() : -1;
    s.media_dur_s = media["durS"].is<int>() ? media["durS"].as<int>() : -1;
    s.media_can_seek = media["canSeek"].as<bool>();
    s.media_art_id = media["artId"].is<uint32_t>() ? media["artId"].as<uint32_t>() : 0;
    const char *col = media["color"].as<const char *>();
    s.media_accent = (col && col[0] == '#' && strlen(col) == 7) ? static_cast<uint32_t>(strtoul(col + 1, nullptr, 16)) : 0;
  }

  s.temp_count = 0;
  for (JsonObjectConst t : p["temps"].as<JsonArrayConst>()) {
    if (s.temp_count >= telemetry::kMaxTemps) break;
    const float v = num(t["tempC"]);
    if (!telemetry::has(v)) continue;  // agent only sends valid ones; be defensive anyway
    auto &dst = s.temps[s.temp_count++];
    str(t["name"], dst.name, sizeof(dst.name));
    dst.temp_c = v;
    dst.stale = t["stale"].as<bool>();
  }

  JsonObjectConst clock = p["clock"];
  s.clock_epoch = clock["epoch"].is<uint32_t>() ? clock["epoch"].as<uint32_t>() : 0;
  const int off = clock["utcOffsetMin"].is<int>() ? clock["utcOffsetMin"].as<int>() : 0;
  s.clock_offset_min = static_cast<int16_t>(off >= -14 * 60 && off <= 14 * 60 ? off : 0);
}

int8_t i8(JsonVariantConst v, int8_t missing, int lo, int hi) {
  if (!v.is<float>()) return missing;
  const float f = v.as<float>();
  if (!isfinite(f)) return missing;
  const int r = static_cast<int>(lroundf(f));
  return static_cast<int8_t>(r < lo ? lo : r > hi ? hi : r);
}

void hhmm(JsonVariantConst v, char *dst) {
  const char *s = v.as<const char *>();
  if (s && strlen(s) == 5 && s[2] == ':') {
    memcpy(dst, s, 6);
  } else {
    dst[0] = '\0';
  }
}

void parse_weather(JsonObjectConst p, telemetry::Weather &w) {
  using telemetry::kNoTemp;
  w = telemetry::Weather{};
  if (!p["error"].isNull()) {
    str(p["error"], w.error, sizeof(w.error));
    return;
  }
  JsonObjectConst now = p["now"];
  if (now.isNull()) {
    strlcpy(w.error, "bad_response", sizeof(w.error));
    return;
  }
  w.ok = true;
  w.lat = num(p["lat"]);
  w.lon = num(p["lon"]);
  if (p["tzMin"].is<int>()) w.tz_min = static_cast<int16_t>(p["tzMin"].as<int>());
  w.temp = num(now["t"]);
  w.feels = num(now["feels"]);
  w.humidity = i8(now["rh"], -1, 0, 100);
  w.wind_kmh = now["wind"].is<int>() ? static_cast<int16_t>(now["wind"].as<int>()) : -1;
  w.code = i8(now["code"], -1, 0, 99);
  w.day = now["day"].isNull() ? true : now["day"].as<int>() != 0;
  JsonObjectConst today = p["today"];
  w.today_max = i8(today["max"], kNoTemp, -99, 99);
  w.today_min = i8(today["min"], kNoTemp, -99, 99);
  hhmm(today["rise"], w.sunrise);
  hhmm(today["set"], w.sunset);
  for (JsonObjectConst h : p["hours"].as<JsonArrayConst>()) {
    if (w.hour_count >= telemetry::kWeatherHours) break;
    auto &d = w.hours[w.hour_count++];
    d.hour = i8(h["h"], -1, 0, 23);
    d.code = i8(h["code"], -1, 0, 99);
    d.temp = i8(h["t"], kNoTemp, -99, 99);
    d.pop = i8(h["pop"], -1, 0, 100);
    d.day = h["day"].isNull() ? true : h["day"].as<int>() != 0;
  }
  for (JsonObjectConst x : p["days"].as<JsonArrayConst>()) {
    if (w.day_count >= telemetry::kWeatherDays) break;
    auto &d = w.days[w.day_count++];
    d.weekday = i8(x["wd"], -1, 0, 6);
    d.code = i8(x["code"], -1, 0, 99);
    d.max = i8(x["max"], kNoTemp, -99, 99);
    d.min = i8(x["min"], kNoTemp, -99, 99);
    d.pop = i8(x["pop"], -1, 0, 100);
  }
}

void parse_places(JsonObjectConst p, PlaceResults &r) {
  r = PlaceResults{};
  str(p["q"], r.query, sizeof(r.query));
  r.error = !p["error"].isNull();
  for (JsonObjectConst x : p["results"].as<JsonArrayConst>()) {
    if (r.count >= telemetry::kMaxPlaces) break;
    auto &d = r.places[r.count];
    str(x["name"], d.name, sizeof(d.name));
    str(x["region"], d.region, sizeof(d.region));
    d.lat = num(x["lat"]);
    d.lon = num(x["lon"]);
    if (d.name[0] && telemetry::has(d.lat) && telemetry::has(d.lon)) r.count++;
  }
}

void parse_market(JsonObjectConst p, crypto::Market &m) {
  crypto::reset(m);
  str(p["cur"], m.cur, sizeof(m.cur));
  str(p["error"], m.error, sizeof(m.error));
  m.ts = p["ts"].is<uint32_t>() ? p["ts"].as<uint32_t>() : 0;
  for (JsonObjectConst o : p["coins"].as<JsonArrayConst>()) {
    if (m.count >= crypto::kMaxCoins) break;
    crypto::Coin &c = m.coins[m.count];
    str(o["p"], c.pair, sizeof(c.pair));
    if (!c.pair[0]) continue;
    c.price = dnum(o["price"]);
    c.chg_pct = num(o["chg"]);
    c.chg_abs = dnum(o["chgAbs"]);
    c.hi = dnum(o["hi"]);
    c.lo = dnum(o["lo"]);
    c.vol = dnum(o["vol"]);
    c.mcap = dnum(o["mc"]);
    c.ath = dnum(o["ath"]);
    c.rank = o["rank"].is<int>() ? static_cast<int16_t>(o["rank"].as<int>()) : -1;
    for (JsonVariantConst v : o["spark"].as<JsonArrayConst>()) {
      if (c.spark_n >= crypto::kSparkPoints) break;
      const int iv = v.is<int>() ? v.as<int>() : 50;
      c.spark[c.spark_n++] = static_cast<uint8_t>(iv < 0 ? 0 : iv > 100 ? 100 : iv);
    }
    m.count++;
  }
  JsonObjectConst g = p["global"];
  if (!g.isNull()) {
    m.mcap = dnum(g["mcap"]);
    m.mcap_chg = num(g["mcapChg"]);
    m.btc_dom = num(g["btcDom"]);
    m.fng = g["fng"].is<int>() ? i8(g["fng"], -1, 0, 100) : -1;
    str(g["fngClass"], m.fng_class, sizeof(m.fng_class));
  }
  m.ok = !m.error[0] && m.count > 0;
  if (!m.ok && !m.error[0]) strlcpy(m.error, "empty", sizeof(m.error));
}

void parse_candles(JsonObjectConst p, crypto::Chart &c) {
  crypto::reset(c);
  str(p["p"], c.pair, sizeof(c.pair));
  str(p["cur"], c.cur, sizeof(c.cur));
  str(p["error"], c.error, sizeof(c.error));
  const int r = crypto::range_from_code(p["r"].as<const char *>());
  c.range = static_cast<uint8_t>(r < 0 ? 0 : r);
  for (JsonArrayConst row : p["c"].as<JsonArrayConst>()) {
    if (c.n >= crypto::kMaxCandles) break;
    if (row.size() < 6) continue;
    crypto::Candle &k = c.c[c.n];
    k.t = row[0].as<uint32_t>();
    k.o = num(row[1]);
    k.h = num(row[2]);
    k.l = num(row[3]);
    k.c = num(row[4]);
    k.v = num(row[5]);
    if (isnan(k.o) || isnan(k.h) || isnan(k.l) || isnan(k.c)) continue;
    if (isnan(k.v)) k.v = 0;
    c.n++;
  }
  c.ok = !c.error[0] && r >= 0 && c.n >= 2;
  if (!c.ok && !c.error[0]) strlcpy(c.error, r < 0 ? "bad_range" : "empty", sizeof(c.error));
}

void parse_caps(JsonObjectConst p, Capabilities &c) {
  c = Capabilities{};
  for (JsonVariantConst a : p["actions"].as<JsonArrayConst>()) {
    const char *s = a.as<const char *>();
    if (!s) continue;
    if (!strcmp(s, "volume.set")) c.volume = true;
    if (!strcmp(s, "media.seek")) c.seek = true;
    else if (!strncmp(s, "media.", 6)) c.media = true;
    if (!strcmp(s, "session.lock")) c.lock = true;
    if (!strcmp(s, "system.shutdown") || !strcmp(s, "system.restart")) c.power = true;
  }
  c.auth_required = p["authRequired"].as<bool>();
  c.authenticated = p["authenticated"].isNull() ? true : p["authenticated"].as<bool>();
  str(p["provider"], c.provider, sizeof(c.provider));
  JsonArrayConst notes = p["notes"].as<JsonArrayConst>();
  c.note_count = static_cast<uint8_t>(notes.size() > 255 ? 255 : notes.size());
  if (c.note_count) str(notes[0], c.first_note, sizeof(c.first_note));
}

}  // namespace

ParseResult parse(const char *json, size_t len, Message &out) {
  out.type = MsgType::Unknown;
  if (len > kMaxFrame) return ParseResult::TooLarge;
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return ParseResult::Malformed;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return ParseResult::Malformed;
  JsonVariantConst v = root["v"];
  if (!v.is<int>() || v.is<bool>()) return ParseResult::Malformed;
  if (v.as<int>() != kVersion) return ParseResult::UnsupportedVersion;
  const char *type = root["type"].as<const char *>();
  if (!type) return ParseResult::Malformed;
  JsonVariantConst pv = root["payload"];
  if (!pv.isNull() && !pv.is<JsonObjectConst>()) return ParseResult::Malformed;
  JsonObjectConst p = pv.as<JsonObjectConst>();

  if (!strcmp(type, "telemetry.snapshot")) {
    out.type = MsgType::Snapshot;
    parse_snapshot(root, p, out.snapshot);
  } else if (!strcmp(type, "telemetry.capabilities")) {
    out.type = MsgType::Capabilities;
    parse_caps(p, out.caps);
  } else if (!strcmp(type, "heartbeat")) {
    out.type = MsgType::Heartbeat;
  } else if (!strcmp(type, "error")) {
    out.type = MsgType::Error;
    str(p["code"], out.error.code, sizeof(out.error.code));
    str(p["message"], out.error.message, sizeof(out.error.message));
  } else if (!strcmp(type, "command.result")) {
    out.type = MsgType::CommandResult;
    out.cmd = CommandResultInfo{};
    const char *rid = root["requestId"].as<const char *>();
    char expected[12];
    snprintf(expected, sizeof(expected), "%08lx-", static_cast<unsigned long>(s_boot_nonce));
    if (rid && !strncmp(rid, expected, 9)) out.cmd.request_id = strtoul(rid + 9, nullptr, 10);
    out.cmd.ok = p["ok"].as<bool>();
    out.cmd.applied_value = p["appliedValue"].is<int>() ? p["appliedValue"].as<int>() : -1;
    str(p["action"], out.cmd.action, sizeof(out.cmd.action));
    str(p["error"], out.cmd.error, sizeof(out.cmd.error));
  } else if (!strcmp(type, "weather.data")) {
    out.type = MsgType::WeatherData;
    parse_weather(p, out.weather);
  } else if (!strcmp(type, "weather.results")) {
    out.type = MsgType::WeatherResults;
    parse_places(p, out.places);
  } else if (!strcmp(type, "crypto.data")) {
    out.type = MsgType::CryptoData;
    if (out.market) parse_market(p, *out.market);
  } else if (!strcmp(type, "crypto.candles")) {
    out.type = MsgType::CryptoCandles;
    if (out.chart) parse_candles(p, *out.chart);
  } else if (!strcmp(type, "pair.result") || !strcmp(type, "auth.result")) {
    out.type = type[0] == 'p' ? MsgType::PairResult : MsgType::AuthResult;
    out.auth = AuthInfo{};
    out.auth.ok = p["ok"].as<bool>();
    str(p["token"], out.auth.token, sizeof(out.auth.token));
    str(p["agentId"], out.auth.agent_id, sizeof(out.auth.agent_id));
    str(p["agentName"], out.auth.agent_name, sizeof(out.auth.agent_name));
    str(p["error"], out.auth.error, sizeof(out.auth.error));
  } else {
    return ParseResult::UnknownType;
  }
  return ParseResult::Ok;
}

const char *result_name(ParseResult r) {
  switch (r) {
    case ParseResult::Ok: return "ok";
    case ParseResult::TooLarge: return "too_large";
    case ParseResult::Malformed: return "malformed";
    case ParseResult::UnsupportedVersion: return "unsupported_version";
    case ParseResult::UnknownType: return "unknown_type";
  }
  return "?";
}

size_t build_hello(char *buf, size_t n, const char *client_id, const char *fw) {
  JsonDocument doc;
  doc["v"] = kVersion;
  doc["type"] = "hello";
  JsonObject p = doc["payload"].to<JsonObject>();
  p["clientId"] = client_id;
  p["fw"] = fw;
  p["protocol"] = kVersion;
  p["caps"].to<JsonArray>().add("telemetry");
  const size_t need = measureJson(doc);
  if (need + 1 > n) return 0;
  return serializeJson(doc, buf, n);
}

void set_boot_nonce(uint32_t nonce) { s_boot_nonce = nonce; }

size_t build_command(char *buf, size_t n, uint32_t request_id, const char *action, int32_t value) {
  char rid[24];
  snprintf(rid, sizeof(rid), "%08lx-%lu", static_cast<unsigned long>(s_boot_nonce), static_cast<unsigned long>(request_id));
  JsonDocument doc;
  doc["v"] = kVersion;
  doc["type"] = "command.request";
  doc["requestId"] = rid;
  JsonObject p = doc["payload"].to<JsonObject>();
  p["action"] = action;
  if (value >= 0) p["value"] = value;
  if (measureJson(doc) + 1 > n) return 0;
  return serializeJson(doc, buf, n);
}

size_t build_pair_request(char *buf, size_t n, const char *code, const char *name) {
  JsonDocument doc;
  doc["v"] = kVersion;
  doc["type"] = "pair.request";
  JsonObject p = doc["payload"].to<JsonObject>();
  p["code"] = code;
  p["name"] = name;
  if (measureJson(doc) + 1 > n) return 0;
  return serializeJson(doc, buf, n);
}

size_t build_auth_request(char *buf, size_t n, const char *token) {
  JsonDocument doc;
  doc["v"] = kVersion;
  doc["type"] = "auth.request";
  doc["payload"]["token"] = token;
  if (measureJson(doc) + 1 > n) return 0;
  return serializeJson(doc, buf, n);
}

size_t build_weather_search(char *buf, size_t n, const char *query, const char *lang) {
  JsonDocument doc;
  doc["v"] = kVersion;
  doc["type"] = "weather.search";
  JsonObject p = doc["payload"].to<JsonObject>();
  p["q"] = query;
  p["lang"] = lang;
  if (measureJson(doc) + 1 > n) return 0;
  return serializeJson(doc, buf, n);
}

size_t build_weather_request(char *buf, size_t n, float lat, float lon) {
  const int w = snprintf(buf, n, "{\"v\":%d,\"type\":\"weather.request\",\"payload\":{\"lat\":%.4f,\"lon\":%.4f}}",
                         kVersion, static_cast<double>(lat), static_cast<double>(lon));
  return (w > 0 && static_cast<size_t>(w) < n) ? static_cast<size_t>(w) : 0;
}

size_t build_crypto_request(char *buf, size_t n, const char *const *pairs, const char *const *ids, int count,
                            const char *cur) {
  // Hand-built: pairs and ids come from the compiled-in catalog ([A-Z0-9] / [a-z0-9-] only).
  int w = snprintf(buf, n, "{\"v\":%d,\"type\":\"crypto.request\",\"payload\":{\"cur\":\"%s\",\"coins\":[", kVersion, cur);
  for (int i = 0; i < count && w > 0 && static_cast<size_t>(w) < n; i++)
    w += snprintf(buf + w, n - w, "%s{\"p\":\"%s\",\"g\":\"%s\"}", i ? "," : "", pairs[i], ids[i] ? ids[i] : "");
  if (w > 0 && static_cast<size_t>(w) < n) w += snprintf(buf + w, n - w, "]}}");
  return (w > 0 && static_cast<size_t>(w) < n) ? static_cast<size_t>(w) : 0;
}

size_t build_crypto_chart(char *buf, size_t n, const char *pair, uint8_t range, const char *cur) {
  const int w = snprintf(buf, n, "{\"v\":%d,\"type\":\"crypto.chart\",\"payload\":{\"p\":\"%s\",\"r\":\"%s\",\"cur\":\"%s\"}}",
                         kVersion, pair, crypto::range_code(range), cur);
  return (w > 0 && static_cast<size_t>(w) < n) ? static_cast<size_t>(w) : 0;
}

size_t build_art_request(char *buf, size_t n, uint32_t art_id) {
  const int w = snprintf(buf, n, "{\"v\":%d,\"type\":\"media.art_request\",\"payload\":{\"artId\":%lu}}", kVersion,
                         static_cast<unsigned long>(art_id));
  return (w > 0 && static_cast<size_t>(w) < n) ? static_cast<size_t>(w) : 0;
}

size_t build_heartbeat(char *buf, size_t n) {
  const int w = snprintf(buf, n, "{\"v\":%d,\"type\":\"heartbeat\",\"payload\":{}}", kVersion);
  return (w > 0 && static_cast<size_t>(w) < n) ? static_cast<size_t>(w) : 0;
}

}  // namespace proto
