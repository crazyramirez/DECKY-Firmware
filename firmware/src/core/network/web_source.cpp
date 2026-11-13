#include "web_source.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <initializer_list>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/app_controller.h"  // PCD_FW_VERSION
#include "crypto/coin_catalog.h"
#include "esp_heap_caps.h"

// Root certificates bundled with the framework (mbedTLS certificate bundle, the IDF default list).
extern const uint8_t pcd_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t pcd_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace net {
namespace web {
namespace {

const char *TAG = "web";

constexpr const char *kBinance = "data-api.binance.vision";
constexpr const char *kGecko = "api.coingecko.com";
constexpr const char *kFng = "api.alternative.me";
constexpr const char *kMeteo = "api.open-meteo.com";
constexpr const char *kGeocode = "geocoding-api.open-meteo.com";

constexpr uint32_t kIdleCloseMs = 8000;
// Cache ages, as in the PC app (crypto.py): sparklines 5 min, market caps / global 10 min, fear &
// greed 30 min, currency rate 1 min; CoinGecko pauses 20 min after a 429 (no API key).
constexpr uint32_t kSparkMs = 5 * 60 * 1000;
constexpr uint32_t kMetaMs = 10 * 60 * 1000;
constexpr uint32_t kGlobalMs = 10 * 60 * 1000;
constexpr uint32_t kFngMs = 30 * 60 * 1000;
constexpr uint32_t kFxMs = 60 * 1000;
constexpr uint32_t kGeckoBackoffMs = 20 * 60 * 1000;
constexpr uint32_t kRetryMs = 60 * 1000;  // a failed slow refresh is tried again after this

// ---- ArduinoJson documents in PSRAM (forecasts and ticker lists are several KB) -----------------
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
PsramAllocator s_alloc;

// ---- HTTPS -------------------------------------------------------------------------------------
NetworkClientSecure *s_tls = nullptr;
HTTPClient *s_http = nullptr;
char s_host[40] = "";
uint32_t s_used_at = 0;
uint32_t s_ok_at = 0;  // last answered request (any status: the server was reached)

void close_conn() {
  if (s_http) s_http->end();
  if (s_tls) s_tls->stop();
  s_host[0] = '\0';
}

// GET https://host/path. 200: body filled. A kept-alive socket the server closed meanwhile shows up
// as a transport error: the request is retried once on a fresh connection.
int get(const char *host, const char *path, String &body) {
  if (!s_tls) {
    s_tls = new NetworkClientSecure();
    s_http = new HTTPClient();
    s_http->setReuse(true);
  }
  if (strcmp(s_host, host) != 0) close_conn();  // one connection at a time
  int code = -1;
  for (int attempt = 0; attempt < 2; attempt++) {
    body = "";
    // Every time: NetworkClientSecure::stop() wipes the TLS context, certificate bundle hook included
    // (and HTTPClient stops the client itself when the server closed a kept-alive socket).
    s_tls->setCACertBundle(pcd_crt_bundle_start, pcd_crt_bundle_end - pcd_crt_bundle_start);
    s_tls->setHandshakeTimeout(12);
    s_http->setConnectTimeout(8000);
    s_http->setTimeout(10000);
    if (!s_http->begin(*s_tls, host, 443, path, true)) {
      close_conn();
      continue;
    }
    strlcpy(s_host, host, sizeof(s_host));
    s_http->setUserAgent("PCControlDeck/" PCD_FW_VERSION);
    s_http->addHeader("Accept", "application/json");
    code = s_http->GET();
    if (code > 0) s_ok_at = millis() | 1;
    if (code == 200) {
      body = s_http->getString();
      const int size = s_http->getSize();
      s_http->end();  // keeps the socket for the next request to this host
      if (body.length() > 1 && (size <= 0 || static_cast<int>(body.length()) == size)) {
        s_used_at = millis();
        return 200;
      }
      code = -100;  // truncated
    }
    close_conn();  // never reuse a socket after an error
    ESP_LOGW(TAG, "GET %s%.60s -> %d", host, path, code);
    if (code >= 400 && code < 500 && code != 408) break;  // definitive (unknown symbol, rate limit...)
  }
  return code;
}

// ---- small helpers -----------------------------------------------------------------------------
float numf(JsonVariantConst v) {
  if (v.is<float>()) return v.as<float>();
  if (v.is<const char *>()) {
    char *end = nullptr;
    const char *s = v.as<const char *>();
    const float f = strtof(s, &end);
    if (end != s) return f;
  }
  return NAN;
}

double numd(JsonVariantConst v) {
  if (v.is<double>()) return v.as<double>();
  if (v.is<const char *>()) {
    char *end = nullptr;
    const char *s = v.as<const char *>();
    const double d = strtod(s, &end);
    if (end != s) return d;
  }
  return NAN;
}

int8_t i8(JsonVariantConst v, int8_t missing, int lo, int hi) {
  const float f = numf(v);
  if (!isfinite(f)) return missing;
  const int r = static_cast<int>(lroundf(f));
  return static_cast<int8_t>(r < lo ? lo : r > hi ? hi : r);
}

// strlcpy that never cuts a UTF-8 sequence in half (city names: "Logroño", "Zürich"...).
void copy_utf8(char *dst, size_t n, const char *src) {
  if (!n) return;
  size_t len = src ? strlen(src) : 0;
  if (len >= n) {
    len = n - 1;
    while (len > 0 && (static_cast<uint8_t>(src[len]) & 0xC0) == 0x80) len--;  // back to a lead byte
  }
  if (len) memcpy(dst, src, len);
  dst[len] = '\0';
}

void url_encode(const char *s, char *out, size_t n) {
  static const char *kHex = "0123456789ABCDEF";
  size_t k = 0;
  for (; *s && k + 4 < n; s++) {
    const uint8_t c = static_cast<uint8_t>(*s);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out[k++] = static_cast<char>(c);
    } else {
      out[k++] = '%';
      out[k++] = kHex[c >> 4];
      out[k++] = kHex[c & 15];
    }
  }
  out[k] = '\0';
}

// 0 = Monday, from "YYYY-MM-DD" (days-from-civil; 1970-01-01 was a Thursday).
int weekday(const char *iso) {
  int y = atoi(iso), m = atoi(iso + 5), d = atoi(iso + 8);
  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  y -= m <= 2;
  const int era = y / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = static_cast<long>(era) * 146097 + doe - 719468;
  return static_cast<int>((days + 3) % 7);
}

bool due(uint32_t at, uint32_t every, uint32_t now) { return !at || now - at >= every; }

}  // namespace

// ---- weather -------------------------------------------------------------------------------------
bool forecast(float lat, float lon, telemetry::Weather &w) {
  using telemetry::kNoTemp;
  w = telemetry::Weather{};
  char path[480];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
           "&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
           "&timezone=auto&forecast_days=7",
           lat, lon);
  String body;
  if (get(kMeteo, path, body) != 200) {
    strlcpy(w.error, "unavailable", sizeof(w.error));
    return false;
  }
  JsonDocument doc(&s_alloc);
  const DeserializationError err = deserializeJson(doc, body);
  body = String();
  JsonObjectConst cur = doc["current"];
  const char *now_iso = cur["time"].as<const char *>();
  if (err || cur.isNull() || !now_iso || strlen(now_iso) < 13) {
    strlcpy(w.error, "bad_response", sizeof(w.error));
    return false;
  }
  w.ok = true;
  w.lat = roundf(lat * 100) / 100;
  w.lon = roundf(lon * 100) / 100;
  if (doc["utc_offset_seconds"].is<int>()) w.tz_min = static_cast<int16_t>(doc["utc_offset_seconds"].as<int>() / 60);
  const float t = numf(cur["temperature_2m"]), feels = numf(cur["apparent_temperature"]);
  w.temp = isfinite(t) ? roundf(t * 10) / 10 : telemetry::kMissing;
  w.feels = isfinite(feels) ? roundf(feels * 10) / 10 : telemetry::kMissing;
  w.humidity = i8(cur["relative_humidity_2m"], -1, 0, 100);
  const float wind = numf(cur["wind_speed_10m"]);
  w.wind_kmh = isfinite(wind) ? static_cast<int16_t>(lroundf(wind)) : -1;
  w.code = i8(cur["weather_code"], -1, 0, 99);
  w.day = cur["is_day"].isNull() ? true : cur["is_day"].as<int>() != 0;

  // Next hours from the current one, on the place's local clock ("2026-11-13T09:00").
  char stamp[17];
  memcpy(stamp, now_iso, 13);
  memcpy(stamp + 13, ":00", 4);
  JsonObjectConst h = doc["hourly"];
  JsonArrayConst times = h["time"];
  int i = 0;
  for (JsonVariantConst tv : times) {
    const char *ts = tv.as<const char *>();
    if (ts && strcmp(ts, stamp) >= 0) break;
    i++;
  }
  for (; i < static_cast<int>(times.size()) && w.hour_count < telemetry::kWeatherHours; i++) {
    const char *ts = times[i].as<const char *>();
    if (!ts || strlen(ts) < 13) continue;
    auto &d = w.hours[w.hour_count++];
    d.hour = static_cast<int8_t>(atoi(ts + 11));
    d.code = i8(h["weather_code"][i], -1, 0, 99);
    d.temp = i8(h["temperature_2m"][i], kNoTemp, -99, 99);
    d.pop = i8(h["precipitation_probability"][i], -1, 0, 100);
    d.day = h["is_day"][i].isNull() ? true : h["is_day"][i].as<int>() != 0;
  }

  // Today (max / min / sunrise / sunset), then the next days.
  char today[11];
  memcpy(today, now_iso, 10);
  today[10] = '\0';
  JsonObjectConst dl = doc["daily"];
  JsonArrayConst dtimes = dl["time"];
  bool have_today = false;
  for (size_t k = 0; k < dtimes.size(); k++) {
    const char *ds = dtimes[k].as<const char *>();
    if (!ds || strcmp(ds, today) < 0) continue;
    if (!have_today) {
      have_today = true;
      w.today_max = i8(dl["temperature_2m_max"][k], kNoTemp, -99, 99);
      w.today_min = i8(dl["temperature_2m_min"][k], kNoTemp, -99, 99);
      const char *rise = dl["sunrise"][k].as<const char *>();
      const char *set = dl["sunset"][k].as<const char *>();
      if (rise && strlen(rise) >= 16) copy_utf8(w.sunrise, sizeof(w.sunrise), rise + 11);
      if (set && strlen(set) >= 16) copy_utf8(w.sunset, sizeof(w.sunset), set + 11);
      continue;
    }
    if (w.day_count >= telemetry::kWeatherDays) break;
    auto &d = w.days[w.day_count++];
    d.weekday = static_cast<int8_t>(weekday(ds));
    d.code = i8(dl["weather_code"][k], -1, 0, 99);
    d.max = i8(dl["temperature_2m_max"][k], kNoTemp, -99, 99);
    d.min = i8(dl["temperature_2m_min"][k], kNoTemp, -99, 99);
    d.pop = i8(dl["precipitation_probability_max"][k], -1, 0, 100);
  }
  return true;
}

int search(const char *query, const char *lang, telemetry::Place *out, int max) {
  // Same normalisation as the PC app: collapsed spaces, at most 60 characters, 2 or more.
  char q[64];
  size_t k = 0;
  bool space = false;
  for (const char *p = query; p && *p && k + 1 < sizeof(q) && k < 60; p++) {
    if (*p == ' ' || *p == '\t') {
      space = k > 0;
      continue;
    }
    if (space && k + 2 < sizeof(q)) q[k++] = ' ';
    space = false;
    q[k++] = *p;
  }
  q[k] = '\0';
  if (k < 2) return 0;
  char enc[200];
  url_encode(q, enc, sizeof(enc));
  char path[280];
  snprintf(path, sizeof(path), "/v1/search?name=%s&count=%d&language=%s&format=json", enc, telemetry::kMaxPlaces,
           lang && !strcmp(lang, "en") ? "en" : "es");
  String body;
  if (get(kGeocode, path, body) != 200) return -1;
  JsonDocument doc(&s_alloc);
  if (deserializeJson(doc, body)) return -1;
  int n = 0;
  for (JsonObjectConst r : doc["results"].as<JsonArrayConst>()) {
    if (n >= max) break;
    const char *name = r["name"].as<const char *>();
    const float la = numf(r["latitude"]), lo = numf(r["longitude"]);
    if (!name || !*name || !isfinite(la) || !isfinite(lo) || fabsf(la) > 90 || fabsf(lo) > 180) continue;
    telemetry::Place &p = out[n++];
    p = telemetry::Place{};
    copy_utf8(p.name, sizeof(p.name), name);
    // "Region, Country" without repeating the name (e.g. Madrid · Comunidad de Madrid, España).
    char region[96] = "";
    for (const char *part : {r["admin1"].as<const char *>(), r["country"].as<const char *>()}) {
      if (!part || !*part || !strcmp(part, name)) continue;
      if (region[0]) strlcat(region, ", ", sizeof(region));
      strlcat(region, part, sizeof(region));
    }
    copy_utf8(p.region, sizeof(p.region), region);
    p.lat = roundf(la * 10000) / 10000;
    p.lon = roundf(lo * 10000) / 10000;
  }
  return n;
}

// ---- crypto --------------------------------------------------------------------------------------
namespace {

struct CoinCache {
  int8_t idx = -1;  // catalog index; -1 = free slot
  bool has_ticker = false;
  bool unknown = false;  // Binance does not list the pair
  uint32_t unknown_at = 0;
  double price = NAN, chg_abs = NAN, hi = NAN, lo = NAN, vol = NAN;
  float chg_pct = NAN;
  uint8_t spark[crypto::kSparkPoints] = {};
  uint8_t spark_n = 0;
  uint32_t spark_at = 0;  // last attempt
  double mcap = NAN, ath = NAN;
  int16_t rank = -1;
  uint32_t used_at = 0;
};
constexpr int kCacheSlots = crypto::kMaxCoins * 2;
CoinCache s_cc[kCacheSlots];

struct {
  double mcap = NAN;
  float mcap_chg = NAN, btc_dom = NAN;
  uint32_t at = 0;
} s_global;
struct {
  int8_t fng = -1;
  char cls[20] = "";
  uint32_t at = 0;
} s_fng;
uint32_t s_meta_at = 0;          // last CoinGecko /coins/markets attempt
uint32_t s_gecko_blocked_at = 0;  // last 429
char s_fx_cur[4] = "USD";
double s_fx = 1.0;
uint32_t s_fx_at = 0;
char s_fx_want[4] = "";

CoinCache *slot(int8_t idx, bool create) {
  CoinCache *free_slot = nullptr, *oldest = &s_cc[0];
  for (auto &c : s_cc) {
    if (c.idx == idx) {
      c.used_at = millis();
      return &c;
    }
    if (c.idx < 0 && !free_slot) free_slot = &c;
    if (c.used_at < oldest->used_at) oldest = &c;
  }
  if (!create) return nullptr;
  CoinCache *c = free_slot ? free_slot : oldest;
  *c = CoinCache{};
  c->idx = idx;
  c->used_at = millis();
  return c;
}

bool valid(int8_t idx) { return idx >= 0 && idx < COIN_CATALOG_COUNT; }

bool gecko_ok() { return !s_gecko_blocked_at || millis() - s_gecko_blocked_at >= kGeckoBackoffMs; }

void gecko_failed(int code) {
  if (code == 429) {
    s_gecko_blocked_at = millis();
    if (!s_gecko_blocked_at) s_gecko_blocked_at = 1;
    ESP_LOGI(TAG, "CoinGecko rate limit (429): pausing it for 20 min");
  }
}

// USD -> display currency, from Binance (EURUSDT quotes USD per EUR; USDTxxx quotes xxx per USD).
void refresh_fx(const char *cur) {
  if (!strcmp(cur, "USD")) {
    strlcpy(s_fx_cur, "USD", sizeof(s_fx_cur));
    s_fx = 1.0;
    s_fx_at = millis();
    strlcpy(s_fx_want, cur, sizeof(s_fx_want));
    return;
  }
  const bool same = !strcmp(s_fx_want, cur);
  if (same && !due(s_fx_at, kFxMs, millis())) return;
  static const struct {
    const char *cur, *sym;
    bool invert;
  } kFx[] = {{"EUR", "EURUSDT", true},  {"MXN", "USDTMXN", false}, {"ARS", "USDTARS", false},
             {"COP", "USDTCOP", false}, {"BRL", "USDTBRL", false}};
  const char *sym = nullptr;
  bool invert = false;
  for (const auto &f : kFx)
    if (!strcmp(f.cur, cur)) {
      sym = f.sym;
      invert = f.invert;
    }
  strlcpy(s_fx_want, cur, sizeof(s_fx_want));
  s_fx_at = millis();
  double rate = NAN;
  if (sym) {
    char path[64];
    snprintf(path, sizeof(path), "/api/v3/ticker/price?symbol=%s", sym);
    String body;
    if (get(kBinance, path, body) == 200) {
      JsonDocument doc(&s_alloc);
      if (!deserializeJson(doc, body)) {
        const double p = numd(doc["price"]);
        if (isfinite(p) && p > 0) rate = invert ? 1.0 / p : p;
      }
    }
  }
  if (isfinite(rate)) {
    strlcpy(s_fx_cur, cur, sizeof(s_fx_cur));
    s_fx = rate;
  } else if (!same || strcmp(s_fx_cur, cur) != 0) {
    // No rate for a new currency: USD (labelled as such) rather than wrong numbers.
    strlcpy(s_fx_cur, "USD", sizeof(s_fx_cur));
    s_fx = 1.0;
  }  // else: keep the last good rate for this currency
}

void apply_ticker(CoinCache &c, JsonObjectConst r) {
  c.price = numd(r["lastPrice"]);
  c.chg_pct = numf(r["priceChangePercent"]);
  c.chg_abs = numd(r["priceChange"]);
  c.hi = numd(r["highPrice"]);
  c.lo = numd(r["lowPrice"]);
  c.vol = numd(r["quoteVolume"]);
  c.has_ticker = isfinite(c.price);
}

// Binance klines -> rows. Each row: [openTime, "o", "h", "l", "c", "v", closeTime, "quoteVol", ...].
struct KRow {
  uint32_t t;
  float o, h, l, c, v;
};

int parse_klines(const char *s, KRow *out, int max) {
  const char *p = s ? strchr(s, '[') : nullptr;
  if (!p) return -1;
  p++;
  int n = 0;
  for (;;) {
    while (*p && *p != '[' && *p != ']') p++;
    if (*p != '[') break;  // end of the outer array
    p++;
    double f[8];
    int k = 0;
    while (*p && *p != ']') {
      while (*p == ' ' || *p == ',' || *p == '"') p++;
      if (*p == ']' || !*p) break;
      char *end = nullptr;
      const double v = strtod(p, &end);
      if (k < 8) f[k] = end != p ? v : NAN;
      k++;
      p = end != p ? end : p + 1;
      while (*p && *p != ',' && *p != ']') p++;
    }
    if (*p == ']') p++;
    if (k >= 8 && n < max && isfinite(f[0]) && isfinite(f[1]) && isfinite(f[2]) && isfinite(f[3]) && isfinite(f[4])) {
      out[n].t = static_cast<uint32_t>(f[0] / 1000.0);
      out[n].o = static_cast<float>(f[1]);
      out[n].h = static_cast<float>(f[2]);
      out[n].l = static_cast<float>(f[3]);
      out[n].c = static_cast<float>(f[4]);
      out[n].v = isfinite(f[7]) ? static_cast<float>(f[7]) : 0.0f;
      n++;
    }
  }
  return n;
}

bool refresh_spark(CoinCache &c) {
  c.spark_at = millis();
  char path[96];
  snprintf(path, sizeof(path), "/api/v3/klines?symbol=%s&interval=30m&limit=%d", COIN_CATALOG[c.idx].pair,
           crypto::kSparkPoints);
  String body;
  if (get(kBinance, path, body) != 200) return false;
  KRow rows[crypto::kSparkPoints];
  const int n = parse_klines(body.c_str(), rows, crypto::kSparkPoints);
  if (n <= 0) return false;
  float lo = rows[0].c, hi = rows[0].c;
  for (int i = 1; i < n; i++) {
    lo = fminf(lo, rows[i].c);
    hi = fmaxf(hi, rows[i].c);
  }
  for (int i = 0; i < n; i++) {
    const int v = hi > lo ? static_cast<int>(lroundf((rows[i].c - lo) / (hi - lo) * 100.0f)) : 50;
    c.spark[i] = static_cast<uint8_t>(v < 0 ? 0 : v > 100 ? 100 : v);
  }
  c.spark_n = static_cast<uint8_t>(n);
  return true;
}

// Market cap, rank and all-time high for every cached coin with a CoinGecko id (one request).
void refresh_meta(const int8_t *coins, int n) {
  s_meta_at = millis();
  char ids[640] = "";
  for (int i = 0; i < n; i++) {
    if (!valid(coins[i]) || !COIN_CATALOG[coins[i]].cg_id || !COIN_CATALOG[coins[i]].cg_id[0]) continue;
    if (ids[0]) strlcat(ids, ",", sizeof(ids));
    strlcat(ids, COIN_CATALOG[coins[i]].cg_id, sizeof(ids));
  }
  if (!ids[0]) return;
  char path[720];
  snprintf(path, sizeof(path), "/api/v3/coins/markets?vs_currency=usd&per_page=50&ids=%s", ids);
  String body;
  const int code = get(kGecko, path, body);
  if (code != 200) {
    gecko_failed(code);
    return;
  }
  JsonDocument filter;
  filter[0]["id"] = true;
  filter[0]["market_cap"] = true;
  filter[0]["market_cap_rank"] = true;
  filter[0]["ath"] = true;
  JsonDocument doc(&s_alloc);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;
  for (JsonObjectConst r : doc.as<JsonArrayConst>()) {
    const char *id = r["id"].as<const char *>();
    if (!id) continue;
    for (int i = 0; i < n; i++) {
      if (!valid(coins[i]) || strcmp(COIN_CATALOG[coins[i]].cg_id, id) != 0) continue;
      CoinCache *c = slot(coins[i], true);
      c->mcap = numd(r["market_cap"]);
      c->ath = numd(r["ath"]);
      const int rank = r["market_cap_rank"].is<int>() ? r["market_cap_rank"].as<int>() : -1;
      c->rank = static_cast<int16_t>(rank > 0 ? rank : -1);
    }
  }
}

void refresh_global() {
  s_global.at = millis();
  String body;
  const int code = get(kGecko, "/api/v3/global", body);
  if (code != 200) {
    gecko_failed(code);
    return;
  }
  JsonDocument filter;
  filter["data"]["total_market_cap"]["usd"] = true;
  filter["data"]["market_cap_change_percentage_24h_usd"] = true;
  filter["data"]["market_cap_percentage"]["btc"] = true;
  JsonDocument doc(&s_alloc);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;
  JsonObjectConst d = doc["data"];
  s_global.mcap = numd(d["total_market_cap"]["usd"]);
  s_global.mcap_chg = numf(d["market_cap_change_percentage_24h_usd"]);
  s_global.btc_dom = numf(d["market_cap_percentage"]["btc"]);
}

void refresh_fng() {
  s_fng.at = millis();
  String body;
  if (get(kFng, "/fng/?limit=1", body) != 200) return;
  JsonDocument doc(&s_alloc);
  if (deserializeJson(doc, body)) return;
  JsonObjectConst r = doc["data"][0];
  const float v = numf(r["value"]);
  if (isfinite(v) && v >= 0 && v <= 100) s_fng.fng = static_cast<int8_t>(lroundf(v));
  const char *cls = r["value_classification"].as<const char *>();
  if (cls) copy_utf8(s_fng.cls, sizeof(s_fng.cls), cls);
}

}  // namespace

bool prices(const int8_t *coins, int n, const char *cur) {
  refresh_fx(cur);
  const uint32_t now = millis();
  // One batch for every listed pair; an unknown symbol makes Binance reject the batch (400), then
  // each pair is asked on its own and the unknown one remembered for 10 min.
  char syms[320] = "%5B";
  int asked = 0;
  for (int i = 0; i < n; i++) {
    if (!valid(coins[i])) continue;
    CoinCache *c = slot(coins[i], true);
    if (c->unknown && !due(c->unknown_at, 10 * 60 * 1000, now)) continue;
    char item[40];
    snprintf(item, sizeof(item), "%s%%22%s%%22", asked ? "," : "", COIN_CATALOG[coins[i]].pair);
    strlcat(syms, item, sizeof(syms));
    asked++;
  }
  if (!asked) return false;
  strlcat(syms, "%5D", sizeof(syms));
  char path[400];
  snprintf(path, sizeof(path), "/api/v3/ticker/24hr?type=FULL&symbols=%s", syms);
  JsonDocument filter;
  filter[0]["symbol"] = true;
  filter[0]["lastPrice"] = true;
  filter[0]["priceChange"] = true;
  filter[0]["priceChangePercent"] = true;
  filter[0]["highPrice"] = true;
  filter[0]["lowPrice"] = true;
  filter[0]["quoteVolume"] = true;
  String body;
  const int code = get(kBinance, path, body);
  if (code == 200) {
    JsonDocument doc(&s_alloc);
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
    for (JsonObjectConst r : doc.as<JsonArrayConst>()) {
      const char *sym = r["symbol"].as<const char *>();
      for (int i = 0; sym && i < n; i++) {
        if (!valid(coins[i]) || strcmp(COIN_CATALOG[coins[i]].pair, sym) != 0) continue;
        apply_ticker(*slot(coins[i], true), r);
      }
    }
    return true;
  }
  if (code != 400) return false;
  bool any = false;
  for (int i = 0; i < n; i++) {
    if (!valid(coins[i])) continue;
    CoinCache *c = slot(coins[i], true);
    if (c->unknown && !due(c->unknown_at, 10 * 60 * 1000, now)) continue;
    snprintf(path, sizeof(path), "/api/v3/ticker/24hr?type=FULL&symbol=%s", COIN_CATALOG[coins[i]].pair);
    const int one = get(kBinance, path, body);
    if (one == 400) {
      c->unknown = true;
      c->unknown_at = now ? now : 1;
      c->has_ticker = false;
      continue;
    }
    if (one != 200) continue;
    JsonDocument doc(&s_alloc);
    if (deserializeJson(doc, body)) continue;
    apply_ticker(*c, doc.as<JsonObjectConst>());
    any = true;
  }
  return any;
}

bool enrich(const int8_t *coins, int n) {
  const uint32_t now = millis();
  for (int i = 0; i < n; i++) {
    if (!valid(coins[i])) continue;
    CoinCache *c = slot(coins[i], true);
    if (c->unknown || !c->has_ticker) continue;
    if (due(c->spark_at, c->spark_n ? kSparkMs : kRetryMs, now)) {
      refresh_spark(*c);
      return true;
    }
  }
  if (gecko_ok() && due(s_meta_at, kMetaMs, now)) {
    refresh_meta(coins, n);
    return true;
  }
  if (gecko_ok() && due(s_global.at, isfinite(s_global.mcap) ? kGlobalMs : kRetryMs, now)) {
    refresh_global();
    return true;
  }
  if (due(s_fng.at, s_fng.fng >= 0 ? kFngMs : kRetryMs, now)) {
    refresh_fng();
    return true;
  }
  return false;
}

void build(const int8_t *coins, int n, const char *cur, crypto::Market &m) {
  crypto::reset(m);
  const bool fx_ok = !strcmp(s_fx_want, cur);
  const double fx = fx_ok ? s_fx : 1.0;
  strlcpy(m.cur, fx_ok ? s_fx_cur : "USD", sizeof(m.cur));
  bool any = false;
  for (int i = 0; i < n && m.count < crypto::kMaxCoins; i++) {
    if (!valid(coins[i])) continue;
    crypto::Coin &o = m.coins[m.count++];
    crypto::reset(o);
    strlcpy(o.pair, COIN_CATALOG[coins[i]].pair, sizeof(o.pair));
    const CoinCache *c = slot(coins[i], false);
    if (!c) continue;
    if (c->has_ticker) {
      o.price = c->price * fx;
      o.chg_pct = c->chg_pct;
      o.chg_abs = c->chg_abs * fx;
      o.hi = c->hi * fx;
      o.lo = c->lo * fx;
      o.vol = c->vol * fx;
      any = true;
    }
    o.mcap = c->mcap * fx;
    o.ath = c->ath * fx;
    o.rank = c->rank;
    memcpy(o.spark, c->spark, c->spark_n);
    o.spark_n = c->spark_n;
  }
  m.mcap = s_global.mcap * fx;
  m.mcap_chg = s_global.mcap_chg;
  m.btc_dom = s_global.btc_dom;
  m.fng = s_fng.fng;
  strlcpy(m.fng_class, s_fng.cls, sizeof(m.fng_class));
  const time_t t = time(nullptr);
  m.ts = t > 1700000000 ? static_cast<uint32_t>(t) : 0;
  m.ok = any;
  if (!any) strlcpy(m.error, "unavailable", sizeof(m.error));
}

void chart(const char *pair, uint8_t range, const char *cur, crypto::Chart &out) {
  // range -> (kline interval, rows), as in the PC app; "all" is weekly and merged down to 120 rows.
  static const struct {
    const char *interval;
    int limit;
  } kRanges[crypto::kRangeCount] = {{"1m", 60}, {"15m", 96}, {"2h", 84}, {"6h", 120}, {"1d", 90}, {"3d", 122}, {"1w", 1000}};
  crypto::reset(out);
  strlcpy(out.pair, pair, sizeof(out.pair));
  out.range = range < crypto::kRangeCount ? range : 1;
  refresh_fx(cur);
  const bool fx_ok = !strcmp(s_fx_want, cur);
  const double fx = fx_ok ? s_fx : 1.0;
  strlcpy(out.cur, fx_ok ? s_fx_cur : "USD", sizeof(out.cur));
  const auto &rg = kRanges[out.range];
  char path[96];
  snprintf(path, sizeof(path), "/api/v3/klines?symbol=%s&interval=%s&limit=%d", pair, rg.interval, rg.limit);
  String body;
  const int code = get(kBinance, path, body);
  if (code != 200) {
    strlcpy(out.error, code == 400 ? "bad_request" : "unavailable", sizeof(out.error));
    return;
  }
  KRow *rows = static_cast<KRow *>(heap_caps_malloc(sizeof(KRow) * rg.limit, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rows) {
    strlcpy(out.error, "no_memory", sizeof(out.error));
    return;
  }
  const int n = parse_klines(body.c_str(), rows, rg.limit);
  body = String();
  auto put = [&out, fx](const KRow &r) {
    if (out.n >= crypto::kMaxCandles) return;
    crypto::Candle &k = out.c[out.n++];
    k.t = r.t;
    k.o = static_cast<float>(r.o * fx);
    k.h = static_cast<float>(r.h * fx);
    k.l = static_cast<float>(r.l * fx);
    k.c = static_cast<float>(r.c * fx);
    k.v = static_cast<float>(r.v * fx);
  };
  if (n > crypto::kMaxCandles && out.range == static_cast<uint8_t>(crypto::Range::All)) {
    // Buckets of k rows aligned to the END: the newest bucket is complete, the oldest may be partial.
    const int k = (n + crypto::kMaxCandles - 1) / crypto::kMaxCandles;
    int a = 0;
    int b = n % k ? n % k : k;
    while (a < n) {
      KRow g = rows[a];
      for (int j = a + 1; j < b; j++) {
        g.h = fmaxf(g.h, rows[j].h);
        g.l = fminf(g.l, rows[j].l);
        g.v += rows[j].v;
      }
      g.c = rows[b - 1].c;
      put(g);
      a = b;
      b = a + k;
    }
  } else {
    for (int i = n > crypto::kMaxCandles ? n - crypto::kMaxCandles : 0; i < n; i++) put(rows[i]);
  }
  heap_caps_free(rows);
  out.ok = out.n >= 2;
  if (!out.ok) strlcpy(out.error, "empty", sizeof(out.error));
}

bool answered_within(uint32_t ms) { return s_ok_at && millis() - s_ok_at < ms; }

void idle(bool force) {
  if (!s_host[0]) return;
  if (force || millis() - s_used_at >= kIdleCloseMs) close_conn();
}

}  // namespace web
}  // namespace net
