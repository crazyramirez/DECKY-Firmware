#pragma once
// Typed telemetry model shared by the demo source (Hito 1) and the protocol parser (Hito 3).
//
// Rule: an unsupported / absent metric is NaN (maps to JSON null), never 0.
// Use telemetry::has(v) before displaying; the UI renders "--" otherwise.

#include <math.h>
#include <stdint.h>

namespace telemetry {

constexpr float kMissing = NAN;
inline bool has(float v) { return !isnan(v) && !isinf(v); }

constexpr int kMaxStorage = 2;
constexpr int kMaxFans = 6;
constexpr int kNameLen = 40;

struct CpuMetrics {
  char name[kNameLen] = "";
  char temp_sensor_name[24] = "";    // which sensor feeds temp_c ("CPU Package", "CPU (EC)"...)
  float temp_c = kMissing;           // selected package / Tctl sensor
  float core_max_temp_c = kMissing;
  float load_pct = kMissing;
  float freq_mhz = kMissing;
  float package_power_w = kMissing;  // CPU package power, NOT whole-PC consumption
};

struct GpuMetrics {
  char name[kNameLen] = "";
  float temp_c = kMissing;
  float load_pct = kMissing;
  float vram_used_mib = kMissing;
  float vram_total_mib = kMissing;
  float board_power_w = kMissing;
};

struct RamMetrics {
  float used_mib = kMissing;
  float total_mib = kMissing;
};

struct StorageMetrics {
  char name[24] = "";
  float temp_c = kMissing;
};

struct FanMetrics {
  char label[20] = "";
  float rpm = kMissing;
};

constexpr int kMaxTemps = 6;

// Extra temperature sensors reported by the agent (GPU memory junction, board, NVMe...).
struct TempEntry {
  char name[24] = "";
  float temp_c = kMissing;
  bool stale = false;
};

struct Snapshot {
  uint32_t seq = 0;
  uint32_t rx_ms = 0;        // millis() when received / generated
  bool synthetic = false;    // true only for demo-mode data
  char host[24] = "";        // short device name, e.g. "desktop-i9"
  CpuMetrics cpu;
  GpuMetrics gpu;
  RamMetrics ram;
  StorageMetrics storage[kMaxStorage];
  uint8_t storage_count = 0;
  FanMetrics fans[kMaxFans];
  uint8_t fan_count = 0;
  TempEntry temps[kMaxTemps];
  uint8_t temp_count = 0;

  // PC audio + "now playing" (Windows media session). volume_pct < 0 = unknown.
  int8_t volume_pct = -1;
  bool muted = false;
  bool has_media = false;
  bool media_playing = false;
  bool media_can_seek = false;  // the player accepts position changes (media.seek)
  char media_title[48] = "";
  char media_artist[40] = "";
  char media_app[24] = "";
  int32_t media_pos_s = -1;   // -1 = unknown
  int32_t media_dur_s = -1;
  uint32_t media_art_id = 0;  // 0 = no artwork
  uint32_t media_accent = 0;  // 0xRRGGBB, 0 = none

  // Laptop battery (has_battery = false on desktops or when unknown).
  bool has_battery = false;
  int8_t bat_pct = -1;
  int8_t bat_plugged = -1;   // AC adapter: 1 yes, 0 no, -1 unknown
  int8_t bat_charging = -1;  // 1 yes, 0 no, -1 unknown
  int32_t bat_secs_left = -1;  // on battery: estimated time to empty, -1 unknown
  float bat_remaining_wh = kMissing;
  float bat_full_wh = kMissing;
  float bat_rate_w = kMissing;  // > 0 charging, < 0 discharging

  // PC clock (agent >= 0.5): UTC epoch + the PC's UTC offset. clock_epoch == 0: not sent.
  uint32_t clock_epoch = 0;
  int16_t clock_offset_min = 0;
};

// Local weather (agent "weather.data", Open-Meteo). Temperatures in °C, kNoTemp = missing.
// Weather codes are WMO (0 clear ... 99 thunderstorm); -1 = missing.
constexpr int8_t kNoTemp = -128;
constexpr int kWeatherHours = 8;
constexpr int kWeatherDays = 5;

constexpr int16_t kNoTz = INT16_MIN;

struct WeatherHour {
  int8_t hour = -1;  // local hour 0..23 at the place
  int8_t code = -1;
  int8_t temp = kNoTemp;
  int8_t pop = -1;  // precipitation probability %
  bool day = true;
};

struct WeatherDay {
  int8_t weekday = -1;  // 0 = Monday
  int8_t code = -1;
  int8_t max = kNoTemp, min = kNoTemp;
  int8_t pop = -1;
};

struct Weather {
  bool ok = false;
  char error[24] = "";
  float lat = kMissing, lon = kMissing;
  float temp = kMissing, feels = kMissing;
  int8_t humidity = -1;
  int16_t wind_kmh = -1;
  int8_t code = -1;
  bool day = true;
  int8_t today_max = kNoTemp, today_min = kNoTemp;
  char sunrise[6] = "", sunset[6] = "";  // "07:58", place's local time
  WeatherHour hours[kWeatherHours];
  uint8_t hour_count = 0;
  WeatherDay days[kWeatherDays];
  uint8_t day_count = 0;
  int16_t tz_min = kNoTz;  // the place's UTC offset (minutes); sets the clock when no PC is connected
};

// City search ("weather.results").
struct Place {
  char name[40] = "";
  char region[48] = "";
  float lat = kMissing, lon = kMissing;
};
constexpr int kMaxPlaces = 6;

}  // namespace telemetry
