#pragma once
// User preferences persisted in NVS (namespace "pcd"). No secrets live here: Wi-Fi and pairing
// credentials get their own namespace in Hito 3/4 and are never logged.

#include <stdint.h>

namespace storage {

enum class Lang : uint8_t { Es = 0, En = 1 };

// Tabs the user can reorder (navigation rail or bottom bar; Settings is always last). Stored in NVS.
enum class Tab : uint8_t { Monitor = 0, Thermals = 1, Player = 2, Today = 3, Crypto = 4 };
constexpr int kTabCount = 5;

// Crypto tab: display currencies (same set as the PC app supports).
enum class Currency : uint8_t { Usd = 0, Eur, Mxn, Ars, Cop, Brl };
constexpr int kCurrencyCount = 6;
constexpr int kCryptoRanges = 7;  // 1H 24H 7D 1M 3M 1Y ALL
constexpr int kCryptoMaxCoins = 12;

// Screensaver (replaces auto sleep while enabled): wallpaper, clock, date and the chosen data.
enum class SsBg : uint8_t { Fixed = 0, Random = 1, Cycle = 2 };
enum SsShow : uint8_t { kSsWeather = 1, kSsPc = 2, kSsMedia = 4, kSsCrypto = 8 };
struct Screensaver {
  bool enabled = false;
  uint8_t wait_min = 5;     // idle minutes before it starts: 1, 2, 5, 10, 30
  uint8_t bg_mode = static_cast<uint8_t>(SsBg::Cycle);
  uint8_t bg = 0;           // wallpaper index (fixed image; first image of the cycle)
  uint8_t cycle_min = 5;    // wallpaper change period in cycle mode: 1, 5, 15, 30
  uint8_t show = kSsWeather | kSsPc | kSsMedia | kSsCrypto;
  uint8_t dim = 100;        // backlight while showing, % of the normal brightness: 100, 60, 30
};

// Today: the main place plus more cities (their weather and local time, one swipe away).
constexpr int kMaxCities = 6;  // the main place included
constexpr int kMaxMoreCities = kMaxCities - 1;
constexpr int16_t kCityNoTz = INT16_MIN;
struct City {
  char name[40] = "";
  float lat = 0.0f, lon = 0.0f;
  int16_t tz_min = kCityNoTz;  // last known UTC offset there (minutes): its clock before a forecast arrives
};

struct Settings {
  uint8_t brightness = 80;     // %
  Lang lang = Lang::Es;
  bool alerts_enabled = true;
  bool demo_mode = false;      // developer aid only (USB console "demo on"); never persisted on
  uint8_t tab_order[kTabCount] = {0, 1, 2, 3, 4};
  uint8_t tab_hidden = 0;  // bit (1 << Tab) set = tab not shown in the tab bar (Settings always is)
  uint8_t refresh_s = 5;  // Monitor / Thermals / detail repaint period: 1, 2 or 5 s (data still arrives at 1 Hz)
  uint8_t auto_sleep_min = 0;  // sleep after this many minutes without a touch: 0 (never), 1, 2, 5, 10
                              // (deep sleep with PCD_HAS_DEEP_SLEEP, else the screen turns off)
  uint8_t button_action = 0;   // sleep button (PCD_HAS_SLEEP_BUTTON): 0 = deep sleep, 1 = screen off/on
  char device_name[24] = "PC";  // short label shown in the top bar
  // Today tab: weather place (chosen with the city search), units and clock format.
  char place_name[40] = "";  // "" = no place chosen
  float place_lat = 0.0f, place_lon = 0.0f;
  // More cities for Today, after the main place (app/cities.h). Only with a main place.
  City more_cities[kMaxMoreCities];
  uint8_t more_count = 0;
  bool temp_f = false;    // °F instead of °C
  bool clock_12h = false; // 12-hour clock instead of 24-hour
  bool today_bg = true;   // Today: picture of the current weather behind the page (else the tint)
  int16_t utc_offset_min = 0;  // last known PC UTC offset (clock before the PC connects)
  // Crypto tab: watched coins (catalog symbols, comma separated, <= kCryptoMaxCoins), currency,
  // refresh period of the prices (s), chart style (0 line, 1 candles) and last chart range.
  char crypto_coins[96] = "BTC,ETH,SOL,XRP,BNB,DOGE";
  uint8_t crypto_cur = static_cast<uint8_t>(Currency::Eur);
  uint8_t crypto_refresh_s = 30;  // 15, 30 or 60
  uint8_t crypto_style = 0;
  uint8_t crypto_range = 1;       // 24H
  Screensaver ss;
  float cpu_warn_c = 85.0f;
  float cpu_crit_c = 95.0f;
  float gpu_warn_c = 80.0f;
  float gpu_crit_c = 90.0f;
};

bool valid_auto_sleep(uint8_t minutes);
bool valid_crypto_refresh(uint8_t seconds);
bool valid_screensaver(const Screensaver &ss);
bool valid_tab_order(const uint8_t order[kTabCount]);  // a permutation of 0..kTabCount-1
bool load(Settings &out);        // falls back to defaults on any missing key
bool save(const Settings &s);
bool factory_reset();            // clears the "pcd" namespace only

}  // namespace storage
