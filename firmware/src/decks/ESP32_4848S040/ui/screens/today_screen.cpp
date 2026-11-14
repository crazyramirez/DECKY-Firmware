// Today: a glanceable home screen.
//   clock (72 px) + date
//   now:        condition icon, temperature, condition, max/min, the city (one dot per city; tap: the
//               Cities sheet) · humidity, wind, feels like
//   next hours: 6 columns (hour, icon, temperature, rain chance)
//   next days:  5 rows (day, icon, rain chance, min, range bar on the week's scale, max)
//   sunrise / sunset · data source
// Background: a picture of the current weather, day or night (assets/weather_bg.c), with a cold / warm
// veil from the temperature; with Settings > Time & weather > Weather background off, a tint from the
// current condition (warm for sun, indigo at night...).
// Without a place it shows a call to action that opens the city search.
// With more than one city (app/cities.h) a horizontal swipe changes city: the clock, the date, the
// weather and the picture are that city's.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <initializer_list>

#include "app/cities.h"
#include "app/state_store.h"
#include "assets/weather_bg.h"
#include "ui/cities_ui.h"
#include "ui/ui_internal.h"
#include "ui/wallpaper.h"

#if WEATHER_BG_STAND_INS > 0
#warning "Today weather backgrounds are development stand-ins: add the real images (tools/weather_backgrounds) before a release"
#endif

namespace ui {

void fmt_deg(char *buf, size_t n, float temp_c) {
  if (!telemetry::has(temp_c)) {
    snprintf(buf, n, "--°");
    return;
  }
  const float v = app::state().settings.temp_f ? temp_c * 9.0f / 5.0f + 32.0f : temp_c;
  snprintf(buf, n, "%d°", static_cast<int>(lroundf(v)));
}

namespace today {
namespace {

using namespace theme;
using telemetry::kNoTemp;

enum class Wx : uint8_t { Clear, MostlyClear, Partly, Overcast, Fog, Drizzle, Rain, HeavyRain, Showers, Snow, Storm };

Wx kind(int code) {
  if (code <= 0) return Wx::Clear;
  if (code == 1) return Wx::MostlyClear;
  if (code == 2) return Wx::Partly;
  if (code == 3) return Wx::Overcast;
  if (code == 45 || code == 48) return Wx::Fog;
  if (code >= 51 && code <= 57) return Wx::Drizzle;
  if (code == 65 || code == 67 || code == 82) return Wx::HeavyRain;
  if (code >= 61 && code <= 67) return Wx::Rain;
  if (code == 80 || code == 81) return Wx::Showers;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Wx::Snow;
  if (code >= 95) return Wx::Storm;
  return Wx::Overcast;
}

const char *icon(Wx k, bool day) {
  switch (k) {
    case Wx::Clear: return day ? PCD_SYM_SUN : PCD_SYM_MOON;
    case Wx::MostlyClear:
    case Wx::Partly: return day ? PCD_SYM_CLOUD_SUN : PCD_SYM_CLOUD_MOON;
    case Wx::Overcast: return PCD_SYM_CLOUD;
    case Wx::Fog: return PCD_SYM_FOG;
    case Wx::Drizzle:
    case Wx::Rain: return PCD_SYM_CLOUD_RAIN;
    case Wx::HeavyRain: return PCD_SYM_CLOUD_HEAVY;
    case Wx::Showers: return day ? PCD_SYM_CLOUD_SUN_RAIN : PCD_SYM_CLOUD_MOON_RAIN;
    case Wx::Snow: return PCD_SYM_SNOW;
    case Wx::Storm: return PCD_SYM_BOLT;
  }
  return PCD_SYM_CLOUD;
}

uint32_t icon_color(Wx k, bool day) {
  switch (k) {
    case Wx::Clear: return day ? kPower : 0xD6DEEC;
    case Wx::MostlyClear:
    case Wx::Partly: return day ? 0xF7D58A : 0xC9D3E3;
    case Wx::Overcast:
    case Wx::Fog: return 0xC3CDDD;
    case Wx::Drizzle:
    case Wx::Rain:
    case Wx::Showers: return kCpu;
    case Wx::HeavyRain: return 0x5AA9FF;
    case Wx::Snow: return 0xE8F3FF;
    case Wx::Storm: return kPower;
  }
  return kMuted;
}

// Page tint (top of the background gradient).
uint32_t tint(Wx k, bool day) {
  if (!day) return k == Wx::Storm ? 0x2E2058 : 0x1C2150;
  switch (k) {
    case Wx::Clear:
    case Wx::MostlyClear: return 0x5A4214;
    case Wx::Partly: return 0x3E3A2E;
    case Wx::Overcast:
    case Wx::Fog: return 0x2B3446;
    case Wx::Drizzle:
    case Wx::Rain:
    case Wx::Showers:
    case Wx::HeavyRain: return 0x123C55;
    case Wx::Snow: return 0x34485E;
    case Wx::Storm: return 0x3A2563;
  }
  return 0x1F2A3C;
}

Str cond_str(Wx k) { return static_cast<Str>(static_cast<int>(Str::WxClear) + static_cast<int>(k)); }

void deg8(char *buf, size_t n, int8_t t) { fmt_deg(buf, n, t == kNoTemp ? NAN : static_cast<float>(t)); }

lv_obj_t *s_page = nullptr;
uint32_t s_tint = 1;
// Weather background (Settings > Time & weather): the picture of the current weather group, day or
// night (assets/weather_bg.c, scrim baked in), with a cold / warm veil from the temperature.
lv_obj_t *s_photo = nullptr;
lv_obj_t *s_veil = nullptr;
wallpaper::Image s_photo_img;
int s_photo_idx = -1;
// clock
lv_obj_t *s_time = nullptr;
lv_obj_t *s_ampm = nullptr;
lv_obj_t *s_date = nullptr;
int s_last_min = -1;
bool s_last_12h = false;
// call to action (no place yet)
lv_obj_t *s_cta = nullptr;
// now
lv_obj_t *s_now = nullptr;
lv_obj_t *s_now_icon = nullptr;
lv_obj_t *s_now_temp = nullptr;
lv_obj_t *s_now_cond = nullptr;
lv_obj_t *s_now_hilo = nullptr;
cities::Pill s_pill;  // which city (one dot per city)
lv_obj_t *s_stats = nullptr;
lv_obj_t *s_stat[3] = {};
// hours / days
constexpr int kHourCols = 6;
lv_obj_t *s_hours_title = nullptr;
lv_obj_t *s_hours = nullptr;
lv_obj_t *s_h_hour[kHourCols] = {}, *s_h_icon[kHourCols] = {}, *s_h_temp[kHourCols] = {}, *s_h_pop[kHourCols] = {};
lv_obj_t *s_days_title = nullptr;
lv_obj_t *s_days = nullptr;
lv_obj_t *s_d_row[telemetry::kWeatherDays] = {};
lv_obj_t *s_d_name[telemetry::kWeatherDays] = {}, *s_d_icon[telemetry::kWeatherDays] = {},
         *s_d_pop[telemetry::kWeatherDays] = {}, *s_d_min[telemetry::kWeatherDays] = {},
         *s_d_bar[telemetry::kWeatherDays] = {}, *s_d_fill[telemetry::kWeatherDays] = {},
         *s_d_max[telemetry::kWeatherDays] = {};
lv_obj_t *s_footer = nullptr;

constexpr int kInner = kScreenW - 2 * kMargin - 2 * 14;  // card content width (428)
constexpr int kBarX = 210, kBarW = kInner - kBarX - 50;  // 168: min · bar · max

void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (!on && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void apply_tint(uint32_t rgb) {
  if (rgb == s_tint) return;
  s_tint = rgb;
  lv_obj_set_style_bg_color(s_page, rgb ? lv_color_mix(c(rgb), c(kBg), 170) : c(kBg), 0);
}

// Weather background group of a WMO code, in the order of assets/weather_bg.c (see
// tools/weather_backgrounds/PROMPTS.md): clear, partly, cloudy, fog, drizzle, rain, storm, snow, sleet.
int bg_group(int code) {
  if (code <= 0) return 0;
  if (code <= 2) return 1;
  if (code == 3) return 2;
  if (code == 45 || code == 48) return 3;
  if (code == 56 || code == 57 || code == 66 || code == 67) return 8;  // freezing drizzle / rain
  if (code >= 51 && code <= 55) return 4;
  if ((code >= 61 && code <= 65) || (code >= 80 && code <= 82)) return 5;
  if (code >= 95) return 6;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 7;
  return 2;
}

// The weather picture for `w`, or none (setting off, no data): false = use the plain tint.
bool apply_photo(const telemetry::Weather *w) {
  const bool on = w != nullptr && app::state().settings.today_bg && WEATHER_BG_COUNT >= 18;
  if (!on) {
    show(s_photo, false);
    show(s_veil, false);
    if (!app::state().settings.today_bg && s_photo_img.px) {  // switched off: give the PSRAM back
      lv_img_set_src(s_photo, nullptr);
      wallpaper::release(s_photo_img);
      s_photo_idx = -1;
    }
    return false;
  }
  const int idx = bg_group(w->code) * 2 + (w->day ? 0 : 1);
  if (idx != s_photo_idx) {
    if (!wallpaper::decode_jpeg(WEATHER_BG[idx].jpg, WEATHER_BG[idx].jpg_size, WEATHER_BG_W, WEATHER_BG_H, s_photo_img))
      return false;
    s_photo_idx = idx;
    lv_img_cache_invalidate_src(&s_photo_img.dsc);  // same buffer, new pixels
    lv_img_set_src(s_photo, &s_photo_img.dsc);
    lv_obj_invalidate(s_photo);
  }
  apply_tint(0);
  show(s_photo, true);
  // Temperature as a light veil: cold below 5 °C, warm above 30 °C, nothing in between.
  const bool cold = telemetry::has(w->temp) && w->temp < 5.0f;
  const bool warm = telemetry::has(w->temp) && w->temp > 30.0f;
  if (cold || warm) {
    lv_obj_set_style_bg_color(s_veil, c(cold ? 0x4A8FE0 : 0xFF8A3D), 0);
    lv_obj_set_style_bg_opa(s_veil, cold ? 34 : 30, 0);
  }
  show(s_veil, cold || warm);
  return true;
}

lv_obj_t *card(lv_obj_t *parent) {
  lv_obj_t *k = make_card(parent);
  lv_obj_set_width(k, LV_PCT(100));
  lv_obj_set_style_pad_all(k, 14, 0);
  lv_obj_set_style_bg_opa(k, LV_OPA_80, 0);  // the tint shows through a little
  lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
  return k;
}

lv_obj_t *section_title(lv_obj_t *parent, const char *text) {
  lv_obj_t *h = make_label(parent, font_small(), kMuted, text);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  lv_obj_set_style_pad_left(h, 4, 0);
  lv_obj_set_style_pad_top(h, 4, 0);
  return h;
}

lv_obj_t *fixed(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int w, lv_text_align_t align) {
  lv_obj_t *l = make_label(parent, font, color, "");
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(l, align, 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
  return l;
}

void open_place_cb(lv_event_t *) { place::open(); }

void build_clock(lv_obj_t *page) {
  lv_obj_t *hero = lv_obj_create(page);
  lv_obj_remove_style_all(hero);
  lv_obj_set_size(hero, LV_PCT(100), 100);  // clock line height is 53 px
  lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
  s_time = make_label(hero, font_clock(), kText, "--:--");
  lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 12);  // breathing room under the top bar
  s_ampm = make_label(hero, font_title(), kMuted, "");
  lv_obj_align_to(s_ampm, s_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -16);
  s_date = make_label(hero, font_body(), kMuted, "");
  lv_obj_align(s_date, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void build_cta(lv_obj_t *page) {
  s_cta = card(page);
  lv_obj_set_height(s_cta, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_cta, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_cta, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_cta, 10, 0);
  lv_obj_set_style_pad_ver(s_cta, 18, 0);
  make_label(s_cta, font_value(), kCpu, PCD_SYM_CLOUD_SUN);
  lv_obj_t *t = make_label(s_cta, font_body(), kText, tr(Str::PlaceCta));
  lv_obj_set_width(t, LV_PCT(100));
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *b = lv_btn_create(s_cta);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, 220, 46);
  lv_obj_set_style_radius(b, 23, 0);
  lv_obj_set_style_bg_color(b, c(kCpu), 0);
  lv_obj_set_style_bg_color(b, c(0x3FB3C4), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  char txt[64];
  snprintf(txt, sizeof(txt), PCD_SYM_PIN "  %s", tr(Str::ChoosePlace));
  lv_obj_t *bl = make_label(b, font_body(), kBg, txt);
  lv_obj_center(bl);
  lv_obj_add_event_cb(b, open_place_cb, LV_EVENT_CLICKED, nullptr);
}

void build_now(lv_obj_t *page) {
  s_now = card(page);
  lv_obj_set_height(s_now, 146);
  s_now_icon = make_label(s_now, font_big(), kMuted, PCD_SYM_CLOUD);
  lv_obj_align(s_now_icon, LV_ALIGN_TOP_LEFT, 0, 2);
  s_now_temp = make_label(s_now, font_big(), kText, "--°");
  lv_obj_align(s_now_temp, LV_ALIGN_TOP_LEFT, 60, 0);
  s_now_cond = make_label(s_now, font_body(), kText, "");
  lv_obj_set_width(s_now_cond, 220);
  lv_label_set_long_mode(s_now_cond, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_now_cond, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(s_now_cond, LV_ALIGN_TOP_RIGHT, 0, 2);
  s_now_hilo = make_label(s_now, font_small(), kMuted, "");
  lv_obj_align(s_now_hilo, LV_ALIGN_TOP_RIGHT, 0, 26);
  // The city and how far ahead its clock is, one dot per city on the right: tap for the Cities sheet.
  cities::pill_create(s_pill, s_now, font_small(), false, kInner, 6);
  lv_obj_align(s_pill.root, LV_ALIGN_TOP_LEFT, 0, 54);
  // Divider + stats.
  lv_obj_t *div = lv_obj_create(s_now);
  lv_obj_remove_style_all(div);
  lv_obj_set_size(div, kInner, 1);
  lv_obj_set_style_bg_color(div, c(kBorder), 0);
  lv_obj_set_style_bg_opa(div, LV_OPA_60, 0);
  lv_obj_align(div, LV_ALIGN_TOP_LEFT, 0, 82);
  s_stats = lv_obj_create(s_now);
  lv_obj_remove_style_all(s_stats);
  lv_obj_set_size(s_stats, kInner, 24);
  lv_obj_align(s_stats, LV_ALIGN_TOP_LEFT, 0, 94);
  lv_obj_set_flex_flow(s_stats, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_stats, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (auto &l : s_stat) l = make_label(s_stats, font_small(), kText, "");
}

void build_hours(lv_obj_t *page) {
  s_hours_title = section_title(page, tr(Str::NextHours));
  s_hours = card(page);
  lv_obj_set_height(s_hours, 104);
  lv_obj_set_style_pad_hor(s_hours, 6, 0);
  lv_obj_set_flex_flow(s_hours, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_hours, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < kHourCols; i++) {
    lv_obj_t *col = lv_obj_create(s_hours);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 56, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_h_hour[i] = make_label(col, font_small(), kMuted, "");
    s_h_icon[i] = make_label(col, font_title(), kText, "");
    s_h_temp[i] = make_label(col, font_body(), kText, "");
    s_h_pop[i] = make_label(col, font_small(), kCpu, "");
  }
}

void build_days(lv_obj_t *page) {
  s_days_title = section_title(page, tr(Str::NextDays));
  s_days = card(page);
  lv_obj_set_height(s_days, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(s_days, 6, 0);
  lv_obj_set_flex_flow(s_days, LV_FLEX_FLOW_COLUMN);
  for (int i = 0; i < telemetry::kWeatherDays; i++) {
    lv_obj_t *r = lv_obj_create(s_days);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, kInner, 40);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    if (i > 0) {
      lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
      lv_obj_set_style_border_color(r, c(kBorder), 0);
      lv_obj_set_style_border_opa(r, LV_OPA_50, 0);
      lv_obj_set_style_border_width(r, 1, 0);
    }
    s_d_row[i] = r;
    s_d_name[i] = fixed(r, font_body(), kText, 0, 60, LV_TEXT_ALIGN_LEFT);
    s_d_icon[i] = fixed(r, font_title(), kText, 66, 30, LV_TEXT_ALIGN_CENTER);
    s_d_pop[i] = fixed(r, font_small(), kCpu, 108, 44, LV_TEXT_ALIGN_LEFT);
    s_d_min[i] = fixed(r, font_small(), kMuted, kBarX - 44, 36, LV_TEXT_ALIGN_RIGHT);
    s_d_max[i] = fixed(r, font_small(), kText, kInner - 40, 40, LV_TEXT_ALIGN_RIGHT);
    lv_obj_t *bar = lv_obj_create(r);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, kBarW, 5);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, c(kSurfaceHi), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, kBarX, 0);
    s_d_bar[i] = bar;
    lv_obj_t *fill = lv_obj_create(bar);
    lv_obj_remove_style_all(fill);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_bg_color(fill, c(kCpu), 0);
    lv_obj_set_style_bg_grad_color(fill, c(kPower), 0);
    lv_obj_set_style_bg_grad_dir(fill, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_size(fill, 0, 5);
    s_d_fill[i] = fill;
  }
}

// ---- updates -----------------------------------------------------------------------------------
void fill_now(const telemetry::Weather &w, bool data, const char *status) {
  const auto &cfg = app::state().settings;
  char b[64];
  const Wx k = kind(w.code);
  set_text(s_now_icon, data ? icon(k, w.day) : PCD_SYM_CLOUD);
  set_color(s_now_icon, data ? icon_color(k, w.day) : kBorder);
  fmt_deg(b, sizeof(b), data ? w.temp : NAN);
  set_text(s_now_temp, b);
  set_text(s_now_cond, data ? tr(cond_str(k)) : status);
  set_color(s_now_cond, data ? kText : kMuted);
  if (data && w.today_max != kNoTemp) {
    char mx[12], mn[12];
    deg8(mx, sizeof(mx), w.today_max);
    deg8(mn, sizeof(mn), w.today_min);
    snprintf(b, sizeof(b), LV_SYMBOL_UP " %s  " LV_SYMBOL_DOWN " %s", mx, mn);
    set_text(s_now_hilo, b);
  } else {
    set_text(s_now_hilo, "");
  }
  show(s_stats, data);
  if (data) {
    if (w.humidity >= 0)
      snprintf(b, sizeof(b), PCD_SYM_TINT " %d %%", w.humidity);
    else
      snprintf(b, sizeof(b), PCD_SYM_TINT " --");
    set_text(s_stat[0], b);
    if (w.wind_kmh >= 0) {
      if (cfg.temp_f)
        snprintf(b, sizeof(b), PCD_SYM_WIND " %d mph", static_cast<int>(lroundf(w.wind_kmh * 0.6214f)));
      else
        snprintf(b, sizeof(b), PCD_SYM_WIND " %d km/h", w.wind_kmh);
    } else {
      snprintf(b, sizeof(b), PCD_SYM_WIND " --");
    }
    set_text(s_stat[1], b);
    char f[12];
    fmt_deg(f, sizeof(f), w.feels);
    snprintf(b, sizeof(b), "%s %s", tr(Str::Feels), f);
    set_text(s_stat[2], b);
  }
}

void fill_hours(const telemetry::Weather &w) {
  char b[16];
  for (int i = 0; i < kHourCols; i++) {
    const bool has = i < w.hour_count;
    const auto &h = w.hours[i];
    if (!has) {
      set_text(s_h_hour[i], "");
      set_text(s_h_icon[i], "");
      set_text(s_h_temp[i], "");
      set_text(s_h_pop[i], "");
      continue;
    }
    if (i == 0) {
      set_text(s_h_hour[i], tr(Str::Now));
    } else if (app::state().settings.clock_12h) {
      const int h12 = h.hour % 12 == 0 ? 12 : h.hour % 12;
      snprintf(b, sizeof(b), "%d%s", h12, h.hour < 12 ? "a" : "p");
      set_text(s_h_hour[i], b);
    } else {
      snprintf(b, sizeof(b), "%02d", h.hour);
      set_text(s_h_hour[i], b);
    }
    set_color(s_h_hour[i], i == 0 ? kText : kMuted);
    const Wx k = kind(h.code);
    set_text(s_h_icon[i], icon(k, h.day));
    set_color(s_h_icon[i], icon_color(k, h.day));
    deg8(b, sizeof(b), h.temp);
    set_text(s_h_temp[i], b);
    if (h.pop >= 20) {
      snprintf(b, sizeof(b), "%d%%", h.pop);
      set_text(s_h_pop[i], b);
    } else {
      set_text(s_h_pop[i], "");
    }
  }
}

void fill_days(const telemetry::Weather &w) {
  // Range bars share the week's scale (like a weather app): lowest min .. highest max.
  int lo = 127, hi = -128;
  for (int i = 0; i < w.day_count; i++) {
    if (w.days[i].min != kNoTemp && w.days[i].min < lo) lo = w.days[i].min;
    if (w.days[i].max != kNoTemp && w.days[i].max > hi) hi = w.days[i].max;
  }
  const int span = hi > lo ? hi - lo : 1;
  char b[16];
  for (int i = 0; i < telemetry::kWeatherDays; i++) {
    show(s_d_row[i], i < w.day_count);
    if (i >= w.day_count) continue;
    const auto &d = w.days[i];
    set_text(s_d_name[i], d.weekday >= 0 ? tr(static_cast<Str>(static_cast<int>(Str::WdMon) + d.weekday)) : "");
    const Wx k = kind(d.code);
    set_text(s_d_icon[i], icon(k, true));
    set_color(s_d_icon[i], icon_color(k, true));
    if (d.pop >= 20) {
      snprintf(b, sizeof(b), "%d%%", d.pop);
      set_text(s_d_pop[i], b);
    } else {
      set_text(s_d_pop[i], "");
    }
    deg8(b, sizeof(b), d.min);
    set_text(s_d_min[i], b);
    deg8(b, sizeof(b), d.max);
    set_text(s_d_max[i], b);
    if (d.min != kNoTemp && d.max != kNoTemp) {
      const int x = (d.min - lo) * kBarW / span;
      int wpx = (d.max - d.min) * kBarW / span;
      if (wpx < 6) wpx = 6;
      lv_obj_set_pos(s_d_fill[i], x > kBarW - wpx ? kBarW - wpx : x, 0);
      lv_obj_set_width(s_d_fill[i], wpx);
    } else {
      lv_obj_set_width(s_d_fill[i], 0);
    }
  }
}

}  // namespace

const char *weather_icon(int code, bool day) { return icon(kind(code), day); }
uint32_t weather_color(int code, bool day) { return icon_color(kind(code), day); }
const char *weather_text(int code) { return tr(cond_str(kind(code))); }

void format_date(const struct tm &t, char *out, size_t n) {
  const int wd = (t.tm_wday + 6) % 7;  // Monday first
  const char *day = tr(static_cast<Str>(static_cast<int>(Str::WdMonL) + wd));
  const char *month = tr(static_cast<Str>(static_cast<int>(Str::Jan) + t.tm_mon));
  if (lang() == storage::Lang::En)
    snprintf(out, n, "%s, %s %d", day, month, t.tm_mday);
  else
    snprintf(out, n, "%s, %d de %s", day, t.tm_mday, month);
}

lv_obj_t *create(lv_obj_t *parent) {
  s_page = make_page(parent, true);
  lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_page, c(kBg), 0);
  lv_obj_set_style_bg_grad_color(s_page, c(kBg), 0);
  lv_obj_set_style_bg_grad_dir(s_page, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_stop(s_page, 0, 0);
  lv_obj_set_style_bg_grad_stop(s_page, 200, 0);
  s_tint = 1;
  s_last_min = -1;
  // Weather picture and temperature veil: under everything else (the page padding is kMargin x 10),
  // floating: they stay put while the page scrolls.
  s_photo = lv_img_create(s_page);
  lv_obj_add_flag(s_photo, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(s_photo, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_photo, -theme::kMargin, -10);
  lv_obj_set_size(s_photo, WEATHER_BG_W, WEATHER_BG_H);
  lv_obj_add_flag(s_photo, LV_OBJ_FLAG_HIDDEN);
  s_veil = lv_obj_create(s_page);
  lv_obj_remove_style_all(s_veil);
  lv_obj_add_flag(s_veil, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(s_veil, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_veil, -theme::kMargin, -10);
  lv_obj_set_size(s_veil, WEATHER_BG_W, WEATHER_BG_H);
  lv_obj_add_flag(s_veil, LV_OBJ_FLAG_HIDDEN);
  s_photo_idx = -1;
  build_clock(s_page);
  build_cta(s_page);
  build_now(s_page);
  build_hours(s_page);
  build_days(s_page);
  s_footer = make_label(s_page, font_small(), kMuted, "");
  lv_obj_set_width(s_footer, LV_PCT(100));
  lv_label_set_long_mode(s_footer, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_footer, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(s_footer, 4, 0);
  cities::attach_swipe(s_page);
  update();
  return s_page;
}

void update() {
  if (!s_page) return;
  // The city Today shows (app/cities.h): its forecast, its name, its clock.
  const int city = app::today_city();
  const telemetry::Weather &wx = app::city_weather(city);
  const char *err = app::city_error(city);
  const bool has_place = app::city_count() > 0;
  const bool data = has_place && wx.ok;
  cities::pill_update(s_pill);
  tick();
  show(s_cta, !has_place);
  show(s_now, has_place);
  for (lv_obj_t *o : {s_hours_title, s_hours, s_days_title, s_days}) show(o, data);
  if (!has_place) {
    apply_photo(nullptr);
    apply_tint(0);
    set_text(s_footer, "");
    return;
  }
  const char *status = !strcmp(err, "pc_app_old") ? tr(Str::WeatherOldApp)
                       : err[0]                    ? tr(Str::WeatherFailed)
                       : !app::data_online()       ? tr(Str::WeatherOffline)
                                                   : tr(Str::WeatherLoading);
  fill_now(wx, data, status);
  if (!data) {
    apply_photo(nullptr);
    apply_tint(0);
    set_text(s_footer, "");
    return;
  }
  if (!apply_photo(&wx)) apply_tint(tint(kind(wx.code), wx.day));
  fill_hours(wx);
  fill_days(wx);
  char b[96];
  if (wx.sunrise[0])
    snprintf(b, sizeof(b), PCD_SYM_SUN "  %s %s  ·  %s %s\n%s", tr(Str::Sunrise), wx.sunrise, tr(Str::Sunset),
             wx.sunset, tr(Str::WeatherSource));
  else
    snprintf(b, sizeof(b), "%s", tr(Str::WeatherSource));
  set_text(s_footer, b);
}

void tick() {
  if (!s_time) return;
  struct tm t;
  const auto &cfg = app::state().settings;
  const int city = app::today_city();  // the clock is the shown city's
  if (!app::city_time(city, t)) {
    set_text(s_time, "--:--");
    set_text(s_ampm, "");
    set_text(s_date, "");
    s_last_min = -1;
    return;
  }
  const int stamp = (t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min) * storage::kMaxCities + city;
  if (stamp == s_last_min && cfg.clock_12h == s_last_12h) return;
  s_last_min = stamp;
  s_last_12h = cfg.clock_12h;
  char b[64];
  if (cfg.clock_12h) {
    const int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(b, sizeof(b), "%d:%02d", h12, t.tm_min);
    set_text(s_ampm, t.tm_hour < 12 ? "AM" : "PM");
  } else {
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
    set_text(s_ampm, "");
  }
  set_text(s_time, b);
  lv_obj_align_to(s_ampm, s_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -16);
  format_date(t, b, sizeof(b));
  set_text(s_date, b);
}

}  // namespace today
}  // namespace ui
