#pragma once
// Crypto tab data model ("crypto.data" / "crypto.candles" from the PC app, agent >= 0.6).
// Money values are already converted by the PC app into the currency named in `cur`.
// Missing values are NaN (JSON null), never 0.

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace crypto {

constexpr int kMaxCoins = 12;
constexpr int kSparkPoints = 48;   // last 24 h @ 30 min, normalised 0..100
constexpr int kMaxCandles = 120;
constexpr int kRangeCount = 7;

enum class Range : uint8_t { H1 = 0, H24, D7, M1, M3, Y1, All };

// Protocol codes for the ranges, in Range order.
inline const char *range_code(uint8_t r) {
  static const char *kCodes[kRangeCount] = {"1h", "24h", "7d", "1m", "3m", "1y", "all"};
  return r < kRangeCount ? kCodes[r] : "24h";
}
inline int range_from_code(const char *s) {
  for (int i = 0; s && i < kRangeCount; i++)
    if (!strcmp(s, range_code(static_cast<uint8_t>(i)))) return i;
  return -1;
}

struct Coin {
  char pair[16];
  double price, chg_abs, hi, lo, vol, mcap, ath;
  float chg_pct;
  int16_t rank;  // -1 unknown
  uint8_t spark[kSparkPoints];
  uint8_t spark_n;
};

struct Market {
  bool ok;
  char error[24];  // "" when ok; "pc_app_old", "unavailable", "timeout"...
  char cur[4];     // currency actually used by the PC app ("USD" when it had to fall back)
  uint8_t count;
  Coin coins[kMaxCoins];
  double mcap;      // total market cap
  float mcap_chg;   // % 24 h
  float btc_dom;    // %
  int8_t fng;       // Fear & Greed 0..100, -1 unknown
  char fng_class[20];
  uint32_t ts;      // PC clock (epoch s)
};

struct Candle {
  uint32_t t;  // open time, epoch s
  float o, h, l, c, v;
};

struct Chart {
  bool ok;
  char error[24];
  char pair[16];
  uint8_t range;
  char cur[4];
  uint16_t n;
  Candle c[kMaxCandles];
};

inline void reset(Coin &c) {
  memset(&c, 0, sizeof(c));
  c.price = c.chg_abs = c.hi = c.lo = c.vol = c.mcap = c.ath = NAN;
  c.chg_pct = NAN;
  c.rank = -1;
}

inline void reset(Market &m) {
  memset(&m, 0, sizeof(m));
  for (auto &c : m.coins) reset(c);
  m.mcap = NAN;
  m.mcap_chg = m.btc_dom = NAN;
  m.fng = -1;
}

inline void reset(Chart &c) { memset(&c, 0, sizeof(c)); }

inline bool has(double v) { return !isnan(v) && !isinf(v); }

}  // namespace crypto
