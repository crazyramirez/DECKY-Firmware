#include "app/cities.h"

#include <math.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "network/net_task.h"
#include "ui/ui.h"

namespace app {
namespace {

static_assert(net::kMaxWeatherPlaces >= storage::kMaxCities, "the network task needs a slot per city");
static_assert(telemetry::kNoTz == storage::kCityNoTz, "one 'unknown UTC offset' value");

const telemetry::Weather kNoWeather;

// Forecasts come back with the coordinates rounded to 0.01 degrees (network/net_task.cpp).
bool same_place(float lat_a, float lon_a, float lat_b, float lon_b) {
  return fabsf(lat_a - lat_b) <= 0.011f && fabsf(lon_a - lon_b) <= 0.011f;
}

bool valid_tz(int tz) { return tz >= -14 * 60 && tz <= 14 * 60; }

bool pc_clock() {
  const LinkState l = state().link;
  return l == LinkState::Online || l == LinkState::Stale;
}

void coords(int i, float &lat, float &lon) {
  const storage::Settings &cfg = state().settings;
  lat = i == 0 ? cfg.place_lat : cfg.more_cities[i - 1].lat;
  lon = i == 0 ? cfg.place_lon : cfg.more_cities[i - 1].lon;
}

// UTC offset of a city other than the main place: the stored one, else its forecast's.
bool more_tz(int i, int &tz) {
  const AppState &st = state();
  const storage::City &c = st.settings.more_cities[i - 1];
  const telemetry::Weather &w = st.more_weather[i - 1];
  if (c.tz_min != storage::kCityNoTz && valid_tz(c.tz_min)) {
    tz = c.tz_min;
    return true;
  }
  if (w.ok && w.tz_min != telemetry::kNoTz && valid_tz(w.tz_min)) {
    tz = w.tz_min;
    return true;
  }
  return false;
}

// No PC clock: the main place's UTC offset gives the local time (summer time included).
bool adopt_main_tz() {
  AppState &st = state();
  const int16_t tz = st.weather.tz_min;
  if (!st.weather.ok || tz == telemetry::kNoTz || !valid_tz(tz) || pc_clock() || st.settings.utc_offset_min == tz)
    return false;
  st.settings.utc_offset_min = tz;
  return true;
}

void erase_more(int k) {
  AppState &st = state();
  storage::Settings &cfg = st.settings;
  for (int j = k; j + 1 < cfg.more_count; j++) {
    cfg.more_cities[j] = cfg.more_cities[j + 1];
    st.more_weather[j] = st.more_weather[j + 1];
    memcpy(st.more_error[j], st.more_error[j + 1], sizeof(st.more_error[j]));
  }
  cfg.more_count--;
  cfg.more_cities[cfg.more_count] = storage::City{};
  st.more_weather[cfg.more_count] = telemetry::Weather{};
  st.more_error[cfg.more_count][0] = '\0';
}

void changed() {
  storage::save(state().settings);
  sync_weather_places();
  ui::on_weather();
}

}  // namespace

int city_count() {
  const storage::Settings &cfg = state().settings;
  return cfg.place_name[0] ? 1 + cfg.more_count : 0;
}

const char *city_name(int i) {
  const storage::Settings &cfg = state().settings;
  if (i < 0 || i >= city_count()) return "";
  return i == 0 ? cfg.place_name : cfg.more_cities[i - 1].name;
}

const telemetry::Weather &city_weather(int i) {
  const AppState &st = state();
  if (i < 0 || i >= city_count()) return kNoWeather;
  return i == 0 ? st.weather : st.more_weather[i - 1];
}

const char *city_error(int i) {
  const AppState &st = state();
  if (i < 0 || i >= city_count()) return "";
  return i == 0 ? st.weather_error : st.more_error[i - 1];
}

bool city_time(int i, struct tm &out) {
  if (i <= 0) return local_time(out);
  int tz = 0;
  if (i >= city_count() || !more_tz(i, tz)) return false;
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  const time_t there = now + static_cast<time_t>(tz) * 60;
  gmtime_r(&there, &out);
  return true;
}

bool city_offset(int i, int &minutes) {
  minutes = 0;
  if (i <= 0) return i == 0;
  int tz = 0;
  if (i >= city_count() || !more_tz(i, tz)) return false;
  minutes = tz - state().settings.utc_offset_min;
  return true;
}

int city_day_shift(int i) {
  struct tm here, there;
  if (i <= 0 || !local_time(here) || !city_time(i, there)) return 0;
  if (there.tm_year != here.tm_year) return there.tm_year > here.tm_year ? 1 : -1;
  const int d = there.tm_yday - here.tm_yday;
  return d > 0 ? 1 : d < 0 ? -1 : 0;
}

int today_city() {
  const int i = state().today_city;
  return i < city_count() ? i : 0;
}

void show_city(int i) {
  const int n = city_count();
  if (i >= n) i = n - 1;
  if (i < 0) i = 0;
  if (i == today_city()) return;
  state().today_city = static_cast<uint8_t>(i);
  ui::on_weather();
}

bool step_city(int dir) {
  const int next = today_city() + (dir > 0 ? 1 : -1);
  if (next < 0 || next >= city_count()) return false;
  show_city(next);
  return true;
}

void set_place(const char *name, float lat, float lon) {
  AppState &st = state();
  storage::Settings &cfg = st.settings;
  if (!name || !name[0]) {
    remove_city(0);
    return;
  }
  for (int k = 0; k < (cfg.place_name[0] ? cfg.more_count : 0); k++) {
    if (!same_place(cfg.more_cities[k].lat, cfg.more_cities[k].lon, lat, lon)) continue;
    st.today_city = static_cast<uint8_t>(k + 1);  // already one of the cities: it trades places, Today shows it
    make_main_city(k + 1);
    return;
  }
  if (!cfg.place_name[0]) cfg.more_count = 0;
  strlcpy(cfg.place_name, name, sizeof(cfg.place_name));
  cfg.place_lat = lat;
  cfg.place_lon = lon;
  st.weather = telemetry::Weather{};  // the old place's forecast must not show under the new name
  st.weather_error[0] = '\0';
  st.today_city = 0;
  changed();
}

int add_city(const char *name, float lat, float lon) {
  AppState &st = state();
  storage::Settings &cfg = st.settings;
  if (!name || !name[0]) return -1;
  if (city_count() == 0) {
    set_place(name, lat, lon);
    return 0;
  }
  if (same_place(cfg.place_lat, cfg.place_lon, lat, lon)) return 0;
  for (int k = 0; k < cfg.more_count; k++)
    if (same_place(cfg.more_cities[k].lat, cfg.more_cities[k].lon, lat, lon)) return k + 1;
  if (cfg.more_count >= storage::kMaxMoreCities) return -1;
  const int k = cfg.more_count++;
  storage::City &c = cfg.more_cities[k];
  c = storage::City{};
  strlcpy(c.name, name, sizeof(c.name));
  c.lat = lat;
  c.lon = lon;
  st.more_weather[k] = telemetry::Weather{};
  st.more_error[k][0] = '\0';
  changed();
  return k + 1;
}

void remove_city(int i) {
  AppState &st = state();
  storage::Settings &cfg = st.settings;
  if (i < 0 || i >= city_count()) return;
  if (i == 0 && cfg.more_count == 0) {
    cfg.place_name[0] = '\0';
    cfg.place_lat = cfg.place_lon = 0.0f;
    st.weather = telemetry::Weather{};
    st.weather_error[0] = '\0';
    st.today_city = 0;
    changed();
    return;
  }
  if (i == 0) {  // the next city becomes the main place
    const storage::City &c = cfg.more_cities[0];
    strlcpy(cfg.place_name, c.name, sizeof(cfg.place_name));
    cfg.place_lat = c.lat;
    cfg.place_lon = c.lon;
    st.weather = st.more_weather[0];
    memcpy(st.weather_error, st.more_error[0], sizeof(st.weather_error));
    erase_more(0);
  } else {
    erase_more(i - 1);
  }
  if (st.today_city == i)
    st.today_city = 0;
  else if (st.today_city > i)
    st.today_city--;
  adopt_main_tz();
  changed();
}

void make_main_city(int i) {
  AppState &st = state();
  storage::Settings &cfg = st.settings;
  if (i <= 0 || i >= city_count()) return;
  storage::City &c = cfg.more_cities[i - 1];
  storage::City old;
  strlcpy(old.name, cfg.place_name, sizeof(old.name));
  old.lat = cfg.place_lat;
  old.lon = cfg.place_lon;
  if (st.weather.ok && st.weather.tz_min != telemetry::kNoTz && valid_tz(st.weather.tz_min)) old.tz_min = st.weather.tz_min;
  strlcpy(cfg.place_name, c.name, sizeof(cfg.place_name));
  cfg.place_lat = c.lat;
  cfg.place_lon = c.lon;
  c = old;
  const telemetry::Weather w = st.weather;
  st.weather = st.more_weather[i - 1];
  st.more_weather[i - 1] = w;
  char err[sizeof(st.weather_error)];
  memcpy(err, st.weather_error, sizeof(err));
  memcpy(st.weather_error, st.more_error[i - 1], sizeof(err));
  memcpy(st.more_error[i - 1], err, sizeof(err));
  // Today keeps showing the city it showed.
  if (st.today_city == i)
    st.today_city = 0;
  else if (st.today_city == 0)
    st.today_city = static_cast<uint8_t>(i);
  adopt_main_tz();
  changed();
}

void sync_weather_places() {
  net::WxPlace places[storage::kMaxCities];
  const int n = city_count();
  for (int i = 0; i < n; i++) coords(i, places[i].lat, places[i].lon);
  net::set_weather_places(places, n);
}

int ingest_weather(const telemetry::Weather &w, uint32_t now_ms) {
  AppState &st = state();
  // A failure that names no place (a PC app too old for the weather...) is every city's.
  const bool everywhere = !telemetry::has(w.lat) || !telemetry::has(w.lon);
  const int n = city_count();
  int first = -1;
  bool save = false;
  for (int i = 0; i < n; i++) {
    float lat, lon;
    coords(i, lat, lon);
    if (everywhere ? (w.ok && i > 0) : !same_place(lat, lon, w.lat, w.lon)) continue;
    if (first < 0) first = i;
    telemetry::Weather &dst = i == 0 ? st.weather : st.more_weather[i - 1];
    char *err = i == 0 ? st.weather_error : st.more_error[i - 1];
    if (!w.ok) {
      strlcpy(err, w.error[0] ? w.error : "unavailable", sizeof(st.weather_error));
      continue;
    }
    dst = w;
    err[0] = '\0';
    if (i == 0) {
      st.weather_rx_ms = now_ms;
      save |= adopt_main_tz();
    } else if (w.tz_min != telemetry::kNoTz && valid_tz(w.tz_min) && st.settings.more_cities[i - 1].tz_min != w.tz_min) {
      st.settings.more_cities[i - 1].tz_min = w.tz_min;  // its clock is right from the next power-on
      save = true;
    }
  }
  if (save) storage::save(st.settings);
  return first;
}

}  // namespace app
