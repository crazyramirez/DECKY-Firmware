// Today: a glanceable home screen (1024x600), two calm columns.
//   left (on the background, no card): 150 px clock, long date, current conditions (92 px icon,
//          78 px temperature, max / min, condition; tap: city search), humidity / wind / feels like,
//          sunrise / sunset
//   right: "next hours" card (6 columns: hour, icon, temperature, rain chance) and "next days" card
//          (5 rows: day, icon, rain chance, min, range bar on the week's scale, max)
// The data source credit goes to the top-bar subtitle.
// Background: a picture of the current weather, day or night (assets/weather_bg.c), with a cold / warm
// veil from the temperature; with Settings > Time & weather > Weather background off, a tint from the
// current condition (warm for sun, indigo at night...).
// Rain, drizzle, snow and sleet fall over the picture; storms flash now and then.
// Without a place it shows a call to action that opens the city search.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <initializer_list>

#include "app/power.h"
#include "app/state_store.h"
#include "assets/weather_bg.h"
#include "esp_heap_caps.h"
#include "ui/ui.h"
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

// ---- layout (page inner area 892 x 518, positioned by hand) -------------------------------------
// Left: no card, straight on the condition tint — clock, date, current conditions, stats.
// Right: two glass cards — next hours (6 columns) and next days (5 rows).
// Every label has a fixed width with clipping / dots, so nothing can run into its neighbour.
constexpr int kEdge = 16;                         // extra margin inside the page padding
constexpr int kLeftX = kEdge, kLeftW = 400;
constexpr int kRightX = 448, kRightW = 892 - kEdge - kRightX;  // 428
constexpr int kCardPad = 22;
constexpr int kHoursY = 8, kHoursH = 222;
constexpr int kDaysY = kHoursY + kHoursH + 16, kDaysH = 518 - 8 - kDaysY;  // 264
constexpr int kInnerCardW = kRightW - 2 * kCardPad;  // 384
constexpr int kHourCols = 6;
constexpr int kHourColW = kInnerCardW / kHourCols;  // 64
constexpr int kDayRowY = 32, kDayRowH = 38;
// Day row columns: name | icon | rain | min | range bar | max (10 px between the bar and the values).
constexpr int kNameW = 64, kIconX = 74, kIconW = 38, kPopX = 114, kPopW = 46, kMinX = 162, kMinW = 50;
constexpr int kBarX = 222, kBarW = 104, kMaxX = 336, kMaxW = kInnerCardW - kMaxX;

lv_obj_t *s_page = nullptr;
GradBg s_bg;  // vertical tint from the current condition (hidden when there is none)
uint32_t s_tint = 1;
// Weather background (Settings > Time & weather): the picture of the current weather group, day or
// night (assets/weather_bg.c, scrim baked in), with a cold / warm veil from the temperature.
lv_obj_t *s_photo = nullptr;
lv_obj_t *s_veil = nullptr;
wallpaper::Image s_photo_img;
int s_photo_idx = -1;
// clock
lv_obj_t *s_time = nullptr;
lv_obj_t *s_date = nullptr;
int s_last_min = -1;
bool s_last_12h = false;
// call to action (no place yet)
lv_obj_t *s_cta = nullptr;
// now (left column)
lv_obj_t *s_now = nullptr;
lv_obj_t *s_now_icon = nullptr;
lv_obj_t *s_now_temp = nullptr;
lv_obj_t *s_now_hilo = nullptr;
lv_obj_t *s_now_cond = nullptr;
lv_obj_t *s_stats = nullptr;
lv_obj_t *s_hum = nullptr, *s_wind = nullptr, *s_feels = nullptr, *s_rise = nullptr, *s_set = nullptr;
// next hours
lv_obj_t *s_hours = nullptr;
lv_obj_t *s_h_hour[kHourCols] = {}, *s_h_icon[kHourCols] = {}, *s_h_temp[kHourCols] = {}, *s_h_pop[kHourCols] = {};
// next days
lv_obj_t *s_days = nullptr;
lv_obj_t *s_d_row[telemetry::kWeatherDays] = {};
lv_obj_t *s_d_name[telemetry::kWeatherDays] = {}, *s_d_icon[telemetry::kWeatherDays] = {},
         *s_d_pop[telemetry::kWeatherDays] = {}, *s_d_min[telemetry::kWeatherDays] = {},
         *s_d_fill[telemetry::kWeatherDays] = {}, *s_d_max[telemetry::kWeatherDays] = {};

void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (!on && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void apply_tint(uint32_t rgb) {
  if (rgb == s_tint) return;
  s_tint = rgb;
  if (!s_bg.img) return;
  if (!rgb) {
    lv_obj_add_flag(s_bg.img, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  grad_bg_set(s_bg, lv_color_mix(c(rgb), c(kBg), 170), c(kBg), 220);
  lv_obj_clear_flag(s_bg.img, LV_OBJ_FLAG_HIDDEN);
}

// ---- weather animation (this deck only) ----------------------------------------------------------
// Rain / drizzle / storm streaks, snow flakes, sleet a mix, and a rare soft flash in a storm, moving
// over the weather picture and under the cards. One layer object draws every particle and each
// frame marks one small area per particle (where it was and where it is): LVGL renders only those,
// in bands, at about 25 fps. One lv_obj per particle cost two areas each and a walk of the whole
// object tree per area (17 fps in a storm). Paused off Today and under the screensaver / with the
// screen off.
enum class Fx : uint8_t { None, Drizzle, Rain, Storm, Snow, Sleet };
constexpr int kMaxParticles = 48;
constexpr uint32_t kAnimMs = 40;
struct Particle {
  float x, y, vx, vy, sway;  // layer coordinates (the layer covers the picture)
  uint8_t w, h, opa;
  bool flake;
  lv_area_t drawn;  // where it is on screen now (absolute)
};
Particle s_parts[kMaxParticles];
int s_part_n = 0;
Fx s_fx = Fx::None;
lv_timer_t *s_anim = nullptr;
lv_obj_t *s_fx_layer = nullptr;
lv_obj_t *s_flash = nullptr;
int s_flash_opa = 0;
uint32_t s_next_flash = 0;
uint32_t s_rand = 0x2545F491u;

uint32_t rnd() {
  s_rand ^= s_rand << 13;
  s_rand ^= s_rand >> 17;
  s_rand ^= s_rand << 5;
  return s_rand;
}
float frand(float a, float b) { return a + (b - a) * static_cast<float>(rnd() % 10000) / 10000.0f; }

Fx fx_for(int group) {
  switch (group) {
    case 4: return Fx::Drizzle;
    case 5: return Fx::Rain;
    case 6: return Fx::Storm;
    case 7: return Fx::Snow;
    case 8: return Fx::Sleet;
    default: return Fx::None;
  }
}

constexpr float kFxW = WEATHER_BG_W, kFxH = WEATHER_BG_H;

void spawn(Particle &p, bool anywhere) {
  p.x = frand(0.0f, kFxW + 40);  // a little to the right: streaks drift left as they fall
  p.y = anywhere ? frand(0.0f, kFxH) : frand(-60.0f, -10.0f);
  if (p.flake) {
    p.vy = frand(0.7f, 1.7f);
    p.vx = frand(-0.25f, 0.25f);
    p.sway = frand(0.0f, 6.28f);
  } else {
    p.vy = s_fx == Fx::Drizzle ? frand(8.0f, 11.0f) : frand(15.0f, 21.0f);
    p.vx = -p.vy * 0.14f;
  }
}

// Screen area of a particle at its current position.
lv_area_t area_of(const Particle &p) {
  lv_area_t a;
  a.x1 = static_cast<lv_coord_t>(s_fx_layer->coords.x1 + static_cast<lv_coord_t>(p.x));
  a.y1 = static_cast<lv_coord_t>(s_fx_layer->coords.y1 + static_cast<lv_coord_t>(p.y));
  a.x2 = static_cast<lv_coord_t>(a.x1 + p.w - 1);
  a.y2 = static_cast<lv_coord_t>(a.y1 + p.h - 1);
  return a;
}

void fx_draw_cb(lv_event_t *e) {
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = lv_color_white();
  for (int i = 0; i < s_part_n; i++) {
    const Particle &p = s_parts[i];
    if (!_lv_area_is_on(&p.drawn, ctx->clip_area)) continue;
    d.radius = p.flake ? LV_RADIUS_CIRCLE : 1;
    d.bg_opa = p.opa;
    lv_draw_rect(ctx, &d, &p.drawn);
  }
}

void anim_cb(lv_timer_t *) {
  if (s_fx == Fx::None || !s_fx_layer || current_page() != Page::Today || screensaver::active() ||
      power::is_screen_off())
    return;
  for (int i = 0; i < s_part_n; i++) {
    Particle &p = s_parts[i];
    if (p.flake) {
      p.sway += 0.06f;
      p.x += p.vx + sinf(p.sway) * 0.5f;
    } else {
      p.x += p.vx;
    }
    p.y += p.vy;
    if (p.y > kFxH || p.x < -20 || p.x > kFxW + 40) spawn(p, false);
    const lv_area_t now = area_of(p);
    lv_area_t dirty = p.drawn;  // one area: where it was and where it is (they mostly overlap)
    if (now.x1 < dirty.x1) dirty.x1 = now.x1;
    if (now.y1 < dirty.y1) dirty.y1 = now.y1;
    if (now.x2 > dirty.x2) dirty.x2 = now.x2;
    if (now.y2 > dirty.y2) dirty.y2 = now.y2;
    const bool far = lv_area_get_height(&dirty) > p.h + 40;  // respawned at the top: two areas
    if (far) {
      lv_obj_invalidate_area(s_fx_layer, &p.drawn);
      lv_obj_invalidate_area(s_fx_layer, &now);
    } else {
      lv_obj_invalidate_area(s_fx_layer, &dirty);
    }
    p.drawn = now;
  }
  if (s_fx != Fx::Storm || !s_flash) return;
  if (s_flash_opa > 0) {  // the flash fades over a few frames
    s_flash_opa -= 14;
    if (s_flash_opa <= 0) {
      show(s_flash, false);
    } else {
      lv_obj_set_style_bg_opa(s_flash, static_cast<lv_opa_t>(s_flash_opa), 0);
    }
  } else if (static_cast<int32_t>(lv_tick_get() - s_next_flash) >= 0) {
    s_flash_opa = 56;
    lv_obj_set_style_bg_opa(s_flash, static_cast<lv_opa_t>(s_flash_opa), 0);
    show(s_flash, true);
    s_next_flash = lv_tick_get() + 7000 + rnd() % 9000;
  }
}

void set_fx(Fx fx) {
  if (fx == s_fx || !s_fx_layer) return;
  s_part_n = 0;
  s_fx = fx;
  show(s_flash, false);
  s_flash_opa = 0;
  lv_obj_invalidate(s_fx_layer);  // the old particles go
  show(s_fx_layer, fx != Fx::None);
  if (fx == Fx::None) {
    if (s_anim) lv_timer_pause(s_anim);
    return;
  }
  lv_obj_update_layout(s_fx_layer);  // coords for area_of()
  const int n = fx == Fx::Drizzle ? 22 : fx == Fx::Rain ? 38 : fx == Fx::Storm ? 46 : fx == Fx::Snow ? 34 : 30;
  for (int i = 0; i < n && i < kMaxParticles; i++) {
    Particle &p = s_parts[i];
    p.flake = fx == Fx::Snow || (fx == Fx::Sleet && (i % 2));
    if (p.flake) {
      p.w = p.h = static_cast<uint8_t>(3 + rnd() % 4);
      p.opa = 190;
    } else {
      p.w = 2;
      p.h = static_cast<uint8_t>(fx == Fx::Drizzle ? 9 + rnd() % 5 : 16 + rnd() % 12);
      p.opa = fx == Fx::Drizzle ? 70 : 95;
    }
    spawn(p, true);
    p.drawn = area_of(p);
    s_part_n = i + 1;
  }
  if (!s_anim)
    s_anim = lv_timer_create(anim_cb, kAnimMs, nullptr);
  else
    lv_timer_resume(s_anim);
  s_next_flash = lv_tick_get() + 4000 + rnd() % 6000;
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
    set_fx(Fx::None);
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
  set_fx(fx_for(bg_group(w->code)));
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

lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

// Glass card: surface colour, slightly translucent so the condition tint shows through.
lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *k = make_card(parent);
  lv_obj_set_pos(k, x, y);
  lv_obj_set_size(k, w, h);
  lv_obj_set_style_radius(k, 24, 0);
  lv_obj_set_style_pad_all(k, kCardPad, 0);
  lv_obj_set_style_bg_opa(k, LV_OPA_80, 0);
  lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
  return k;
}

// Fixed-width label (never wider than its slot).
lv_obj_t *slot(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, int w, lv_text_align_t align) {
  lv_obj_t *l = make_label(parent, font, color, "");
  lv_obj_set_size(l, w, lv_font_get_line_height(font));
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(l, align, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

// Optical alignment. Labels are positioned by their line box, but a big glyph sits inside it with
// its own bearings: these give the real ink edges so edges and baselines line up on screen.
struct Ink {
  int l, t, r, b;  // ink box of one glyph relative to its label's top-left
};
Ink ink(const lv_font_t *f, uint32_t letter) {
  const int base = lv_font_get_line_height(f) - f->base_line;  // baseline, from the label top
  Ink k{0, 0, 0, base};
  lv_font_glyph_dsc_t g;
  if (!letter || !lv_font_get_glyph_dsc(f, &g, letter, 0)) return k;
  k.l = g.ofs_x;
  k.r = g.ofs_x + g.box_w;
  k.b = base - g.ofs_y;
  k.t = k.b - g.box_h;
  return k;
}
int baseline(const lv_font_t *f) { return lv_font_get_line_height(f) - f->base_line; }
int cap_height(const lv_font_t *f) {
  const Ink k = ink(f, 'H');
  return k.b - k.t;
}
uint32_t first_letter(const char *s) {
  uint32_t i = 0;
  return s && *s ? _lv_txt_encoded_next(s, &i) : 0;
}

// Card titles: cap top exactly on the card's padding edge, like the left edge.
lv_obj_t *card_title(lv_obj_t *card, Str s) {
  lv_obj_t *t = slot(card, font_tiny(), kMuted, 0, -ink(font_tiny(), 'P').t, kInnerCardW, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_style_text_letter_space(t, 1, 0);
  lv_label_set_text(t, tr(s));
  return t;
}

void open_place_cb(lv_event_t *) { place::open(); }

// Left column grid (page coordinates): one ink edge at kLeftX; the temperature and the second stats
// column share kCol2; the clock's digits sit on the cards' top edge and the last stats line on
// their bottom edge.
constexpr int kTop = kHoursY, kBottom = kDaysY + kDaysH;
constexpr int kCol2 = 150, kCol3 = 300;
constexpr int kNowInset = 12;  // pressed highlight around the current conditions
int s_icon_top = 0;            // page y of the condition icon's ink top
int s_cond_base = 0;           // page y of the condition line's baseline

void place_now() {
  // Icon: ink left on the column edge, ink top on s_icon_top (each glyph has its own bearings).
  const Ink ic = ink(font_weather_xl(), first_letter(lv_label_get_text(s_now_icon)));
  const int ox = kLeftX - kNowInset, oy = s_icon_top - kNowInset;  // s_now origin
  lv_obj_set_pos(s_now_icon, kLeftX - ic.l - ox, s_icon_top - ic.t - oy);
  const int icon_mid = s_icon_top + (ic.b - ic.t) / 2;
  // Temperature + max/min: a block centred on the icon, its ink left on kCol2.
  const Ink d = ink(font_clock(), '0');
  const int digits_h = d.b - d.t, gap = 18, hilo_cap = cap_height(font_small());
  const int top = icon_mid - (digits_h + gap + hilo_cap) / 2;
  lv_obj_set_pos(s_now_temp, kLeftX + kCol2 - d.l - ox, top - d.t - oy);
  lv_obj_set_pos(s_now_hilo, kLeftX + kCol2 - ox, top + digits_h + gap + hilo_cap - baseline(font_small()) - oy);
  // Condition: baseline s_cond_base.
  lv_obj_set_pos(s_now_cond, kLeftX - ox, s_cond_base - baseline(font_title()) - oy);
}

void build_left(lv_obj_t *page) {
  // Clock: digit ink top on the cards' top edge, ink left on the column edge.
  const Ink d = ink(font_clock_xl(), '0');
  s_time = make_label(page, font_clock_xl(), kText, "--:--");
  lv_obj_set_pos(s_time, kLeftX - d.l, kTop - d.t);
  const int clock_bottom = kTop + (d.b - d.t);
  // Date: cap top 24 px under the digits.
  const int date_base = clock_bottom + 24 + cap_height(font_title());
  s_date = slot(page, font_title(), kText, kLeftX, date_base - baseline(font_title()), kLeftW, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_style_text_opa(s_date, LV_OPA_70, 0);

  // Stats: last line's baseline on the cards' bottom edge, 46 px line pitch.
  const int base2 = kBottom, base1 = kBottom - 46;
  const int y1 = base1 - baseline(font_small()), y2 = base2 - baseline(font_small());
  s_stats = plain(page, kLeftX, y1, kLeftW, y2 - y1 + lv_font_get_line_height(font_small()));
  s_hum = slot(s_stats, font_small(), kText, 0, 0, kCol2 - 12, LV_TEXT_ALIGN_LEFT);
  s_wind = slot(s_stats, font_small(), kText, kCol2, 0, kCol3 - kCol2 - 12, LV_TEXT_ALIGN_LEFT);
  s_feels = slot(s_stats, font_small(), kText, kCol3, 0, kLeftW - kCol3, LV_TEXT_ALIGN_LEFT);
  s_rise = slot(s_stats, font_small(), kPower, 0, y2 - y1, kCol2 - 12, LV_TEXT_ALIGN_LEFT);
  s_set = slot(s_stats, font_small(), 0xC9D3E3, kCol2, y2 - y1, kCol3 - kCol2 - 12, LV_TEXT_ALIGN_LEFT);

  // Current conditions (icon, then the condition line 30 px under it) centred between the date's
  // baseline and the first stats line's cap top.
  const Ink typical = ink(font_weather_xl(), first_letter(PCD_SYM_CLOUD_SUN));
  const int icon_h = typical.b - typical.t;
  const int group_h = icon_h + 30 + cap_height(font_title());
  const int free_top = date_base, free_bottom = base1 - cap_height(font_small());
  s_icon_top = free_top + (free_bottom - free_top - group_h) / 2;
  s_cond_base = s_icon_top + group_h;
  s_now = plain(page, kLeftX - kNowInset, s_icon_top - kNowInset, kLeftW + 2 * kNowInset,
                s_cond_base + 14 - s_icon_top + 2 * kNowInset);
  lv_obj_add_flag(s_now, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(s_now, 20, 0);
  lv_obj_set_style_bg_color(s_now, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(s_now, LV_OPA_10, LV_STATE_PRESSED);
  lv_obj_add_event_cb(s_now, open_place_cb, LV_EVENT_CLICKED, nullptr);
  s_now_icon = make_label(s_now, font_weather_xl(), kMuted, PCD_SYM_CLOUD);
  s_now_temp = make_label(s_now, font_clock(), kText, "--°");
  s_now_hilo = make_label(s_now, font_small(), kText, "");
  lv_label_set_recolor(s_now_hilo, true);
  s_now_cond = make_label(s_now, font_title(), kText, "");
  lv_obj_set_size(s_now_cond, kLeftW, lv_font_get_line_height(font_title()));
  lv_label_set_long_mode(s_now_cond, LV_LABEL_LONG_DOT);
  place_now();
}

void build_cta(lv_obj_t *page) {
  s_cta = card(page, kRightX, kHoursY, kRightW, 518 - 8 - kHoursY);
  lv_obj_set_flex_flow(s_cta, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_cta, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_cta, 24, 0);
  make_label(s_cta, font_big(), kCpu, PCD_SYM_CLOUD_SUN);
  lv_obj_t *t = make_label(s_cta, font_body(), kText, tr(Str::PlaceCta));
  lv_obj_set_width(t, LV_PCT(100));
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *b = lv_btn_create(s_cta);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, 330, 64);
  lv_obj_set_style_radius(b, 32, 0);
  lv_obj_set_style_bg_color(b, c(kCpu), 0);
  lv_obj_set_style_bg_color(b, c(0x3FB3C4), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  char txt[64];
  snprintf(txt, sizeof(txt), PCD_SYM_PIN "  %s", tr(Str::ChoosePlace));
  lv_obj_center(make_label(b, font_body(), kBg, txt));
  lv_obj_add_event_cb(b, open_place_cb, LV_EVENT_CLICKED, nullptr);
}

void build_hours(lv_obj_t *page) {
  s_hours = card(page, kRightX, kHoursY, kRightW, kHoursH);
  card_title(s_hours, Str::NextHours);
  for (int i = 0; i < kHourCols; i++) {
    const int x = i * kHourColW;
    // Rows on fixed baselines: hour 56, temperature 148, rain 176; icons' ink centred on 98.
    s_h_hour[i] = slot(s_hours, font_tiny(), kMuted, x, 56 - baseline(font_tiny()), kHourColW, LV_TEXT_ALIGN_CENTER);
    s_h_icon[i] = slot(s_hours, font_weather_m(), kText, x, 0, kHourColW, LV_TEXT_ALIGN_CENTER);
    s_h_temp[i] = slot(s_hours, font_body(), kText, x, 148 - baseline(font_body()), kHourColW, LV_TEXT_ALIGN_CENTER);
    s_h_pop[i] = slot(s_hours, font_tiny(), kCpu, x, 174 - baseline(font_tiny()), kHourColW, LV_TEXT_ALIGN_CENTER);
  }
}

void build_days(lv_obj_t *page) {
  s_days = card(page, kRightX, kDaysY, kRightW, kDaysH);
  card_title(s_days, Str::NextDays);
  for (int i = 0; i < telemetry::kWeatherDays; i++) {
    lv_obj_t *r = plain(s_days, 0, kDayRowY + i * kDayRowH, kInnerCardW, kDayRowH);
    s_d_row[i] = r;
    // Every text of the row on one baseline (caps centred on the row); the icon's ink centred.
    const int base = (kDayRowH + cap_height(font_body())) / 2;
    s_d_name[i] = slot(r, font_body(), kText, 0, base - baseline(font_body()), kNameW, LV_TEXT_ALIGN_LEFT);
    s_d_icon[i] = slot(r, font_weather_s(), kText, kIconX, 0, kIconW, LV_TEXT_ALIGN_CENTER);
    s_d_pop[i] = slot(r, font_tiny(), kCpu, kPopX, base - baseline(font_tiny()), kPopW, LV_TEXT_ALIGN_CENTER);
    s_d_min[i] = slot(r, font_body(), kMuted, kMinX, base - baseline(font_body()), kMinW, LV_TEXT_ALIGN_RIGHT);
    s_d_max[i] = slot(r, font_body(), kText, kMaxX, base - baseline(font_body()), kMaxW, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *bar = plain(r, kBarX, (kDayRowH - 6) / 2, kBarW, 6);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, c(kSurfaceHi), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_t *fill = plain(bar, 0, 0, 0, 6);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_bg_color(fill, c(kCpu), 0);
    lv_obj_set_style_bg_grad_color(fill, c(kPower), 0);
    lv_obj_set_style_bg_grad_dir(fill, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    s_d_fill[i] = fill;
  }
}

// ---- updates -----------------------------------------------------------------------------------
void fill_now(const telemetry::Weather &w, bool data, const char *status) {
  char b[80];
  const Wx k = kind(w.code);
  set_text(s_now_icon, data ? icon(k, w.day) : PCD_SYM_CLOUD);
  set_color(s_now_icon, data ? icon_color(k, w.day) : kBorder);
  fmt_deg(b, sizeof(b), data ? w.temp : NAN);
  set_text(s_now_temp, b);
  place_now();
  set_text(s_now_cond, data ? tr(cond_str(k)) : status);
  set_color(s_now_cond, data ? kText : kMuted);
  if (data && w.today_max != kNoTemp) {
    char mx[12], mn[12];
    deg8(mx, sizeof(mx), w.today_max);
    deg8(mn, sizeof(mn), w.today_min);
    snprintf(b, sizeof(b), "#F4C66F " PCD_SYM_CARET_UP "# %s     #52D5E8 " PCD_SYM_CARET_DOWN "# %s", mx, mn);
    set_text(s_now_hilo, b);
  } else {
    set_text(s_now_hilo, "");
  }
  show(s_stats, data);
  if (!data) return;
  const auto &cfg = app::state().settings;
  if (w.humidity >= 0)
    snprintf(b, sizeof(b), PCD_SYM_TINT "  %d %%", w.humidity);
  else
    snprintf(b, sizeof(b), PCD_SYM_TINT "  --");
  set_text(s_hum, b);
  if (w.wind_kmh < 0)
    snprintf(b, sizeof(b), PCD_SYM_WIND "  --");
  else if (cfg.temp_f)
    snprintf(b, sizeof(b), PCD_SYM_WIND "  %d mph", static_cast<int>(lroundf(w.wind_kmh * 0.6214f)));
  else
    snprintf(b, sizeof(b), PCD_SYM_WIND "  %d km/h", w.wind_kmh);
  set_text(s_wind, b);
  char f[12];
  fmt_deg(f, sizeof(f), w.feels);
  snprintf(b, sizeof(b), PCD_SYM_THERMO "  %s", f);
  set_text(s_feels, b);
  snprintf(b, sizeof(b), PCD_SYM_SUN "  %s", w.sunrise[0] ? w.sunrise : "--:--");
  set_text(s_rise, b);
  snprintf(b, sizeof(b), PCD_SYM_MOON "  %s", w.sunset[0] ? w.sunset : "--:--");
  set_text(s_set, b);
}

void fill_hours(const telemetry::Weather &w) {
  char b[16];
  for (int i = 0; i < kHourCols; i++) {
    const bool has = i < w.hour_count;
    const auto &h = w.hours[i];
    if (!has) {
      for (lv_obj_t *o : {s_h_hour[i], s_h_icon[i], s_h_temp[i], s_h_pop[i]}) set_text(o, "");
      continue;
    }
    if (i == 0) {
      set_text(s_h_hour[i], tr(Str::Now));
    } else if (app::state().settings.clock_12h) {
      snprintf(b, sizeof(b), "%d %s", h.hour % 12 == 0 ? 12 : h.hour % 12, h.hour < 12 ? "AM" : "PM");
      set_text(s_h_hour[i], b);
    } else {
      snprintf(b, sizeof(b), "%02d:00", h.hour);
      set_text(s_h_hour[i], b);
    }
    set_color(s_h_hour[i], i == 0 ? kCpu : kMuted);
    const Wx k = kind(h.code);
    set_text(s_h_icon[i], icon(k, h.day));
    set_color(s_h_icon[i], icon_color(k, h.day));
    const Ink hk = ink(font_weather_m(), first_letter(icon(k, h.day)));
    lv_obj_set_y(s_h_icon[i], 98 - (hk.t + hk.b) / 2);
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
    const Ink ik = ink(font_weather_s(), first_letter(icon(k, true)));
    lv_obj_set_y(s_d_icon[i], kDayRowH / 2 - (ik.t + ik.b) / 2);
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
      if (wpx < 8) wpx = 8;
      lv_obj_set_pos(s_d_fill[i], x > kBarW - wpx ? kBarW - wpx : x, 0);
      lv_obj_set_width(s_d_fill[i], wpx);
    } else {
      lv_obj_set_width(s_d_fill[i], 0);
    }
  }
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  s_page = make_page(parent, false);
  lv_obj_set_layout(s_page, 0);  // hand-placed (892 x 518 inside the padding)
  lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_page, c(kBg), 0);
  // Vertical tint, dithered once into a tiled strip (no banding on the 7" panel, cheap redraws).
  grad_bg_create(s_bg, s_page, false, theme::kContentW, theme::kContentH, theme::kMargin, 15);
  s_tint = 1;
  // Weather picture and temperature veil: over the tint, under everything else.
  s_photo = lv_img_create(s_page);
  lv_obj_add_flag(s_photo, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(s_photo, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_photo, -theme::kMargin, -15);
  lv_obj_set_size(s_photo, WEATHER_BG_W, WEATHER_BG_H);
  lv_obj_add_flag(s_photo, LV_OBJ_FLAG_HIDDEN);
  s_veil = lv_obj_create(s_page);
  lv_obj_remove_style_all(s_veil);
  lv_obj_add_flag(s_veil, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(s_veil, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_veil, -theme::kMargin, -15);
  lv_obj_set_size(s_veil, WEATHER_BG_W, WEATHER_BG_H);
  lv_obj_add_flag(s_veil, LV_OBJ_FLAG_HIDDEN);
  s_flash = lv_obj_create(s_page);  // storm: a short soft flash over the picture
  lv_obj_remove_style_all(s_flash);
  lv_obj_add_flag(s_flash, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(s_flash, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_flash, -theme::kMargin, -15);
  lv_obj_set_size(s_flash, WEATHER_BG_W, WEATHER_BG_H);
  lv_obj_set_style_bg_color(s_flash, c(0xDCE6FF), 0);
  lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
  s_fx_layer = lv_obj_create(s_page);  // rain / snow, drawn by fx_draw_cb()
  lv_obj_remove_style_all(s_fx_layer);
  lv_obj_add_flag(s_fx_layer, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(s_fx_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_fx_layer, -theme::kMargin, -15);
  lv_obj_set_size(s_fx_layer, WEATHER_BG_W, WEATHER_BG_H);
  lv_obj_add_event_cb(s_fx_layer, fx_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_flag(s_fx_layer, LV_OBJ_FLAG_HIDDEN);
  s_photo_idx = -1;
  s_fx = Fx::None;
  s_part_n = 0;
  s_last_min = -1;
  build_left(s_page);
  build_hours(s_page);
  build_days(s_page);
  build_cta(s_page);
  update();
  tick();
  return s_page;
}

void update() {
  if (!s_page) return;
  const auto &st = app::state();
  const bool has_place = st.settings.place_name[0] != '\0';
  const bool data = has_place && st.weather.ok;
  // No place yet: the clock stays, the call to action takes the right column.
  show(s_cta, !has_place);
  show(s_now, has_place);
  show(s_hours, data);
  show(s_days, data);
  if (!data) show(s_stats, false);
  if (current_page() == Page::Today) {
    char sub[80];
    if (has_place)
      snprintf(sub, sizeof(sub), PCD_SYM_PIN " %s%s%s", st.settings.place_name, data ? "  ·  " : "",
               data ? tr(Str::WeatherSource) : "");
    set_subtitle(has_place ? sub : nullptr);
  }
  if (!has_place) {
    apply_photo(nullptr);
    apply_tint(0);
    return;
  }
  const char *status = !strcmp(st.weather_error, "pc_app_old") ? tr(Str::WeatherOldApp)
                       : st.weather_error[0]                    ? tr(Str::WeatherFailed)
                       : !app::data_online()                    ? tr(Str::WeatherOffline)
                                                                : tr(Str::WeatherLoading);
  fill_now(st.weather, data, status);
  if (!data) {
    apply_photo(nullptr);
    apply_tint(0);
    return;
  }
  if (!apply_photo(&st.weather)) apply_tint(tint(kind(st.weather.code), st.weather.day));
  fill_hours(st.weather);
  fill_days(st.weather);
}

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

void tick() {
  if (!s_time) return;
  struct tm t;
  const auto &cfg = app::state().settings;
  if (!app::local_time(t)) {
    set_text(s_time, "--:--");
    set_text(s_date, "");
    s_last_min = -1;
    return;
  }
  const int stamp = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
  if (stamp == s_last_min && cfg.clock_12h == s_last_12h) return;
  s_last_min = stamp;
  s_last_12h = cfg.clock_12h;
  char b[64], d[56];
  if (cfg.clock_12h)
    snprintf(b, sizeof(b), "%d:%02d", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, t.tm_min);
  else
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
  set_text(s_time, b);
  format_date(t, d, sizeof(d));
  // 12 h: AM / PM leads the date line (the 150 px clock stays within its column).
  if (cfg.clock_12h)
    snprintf(b, sizeof(b), "%s  ·  %s", t.tm_hour < 12 ? "AM" : "PM", d);
  else
    snprintf(b, sizeof(b), "%s", d);
  set_text(s_date, b);
}

}  // namespace today
}  // namespace ui
