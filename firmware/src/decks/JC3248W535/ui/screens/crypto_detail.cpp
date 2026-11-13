// Coin detail (ported from the JC4827W543 deck / JC1060P470C CryptoTracker, portrait 296x358):
//   header:  [back] logo · name / symbol                                       [<] [>]
//   price:   live price · 24 h change
//   ranges:  1H 24H 7D 1M 3M 1Y MAX  [line/candles]
//   chart:   anti-aliased line with glow + gradient fill, or candlesticks; volume histogram;
//            period high / low, last-price tag, first / last time. Drag a finger across it to read
//            price and date at any point (the price row follows the finger).
//   stats:   24 h high / low / volume, market cap (+rank), all-time high.
// Swipe left / right outside the chart (or the arrows) to move through the watched coins.

#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <initializer_list>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "crypto/coin_catalog.h"
#include "crypto/crypto_store.h"
#include "crypto/watchlist.h"
#include "esp_heap_caps.h"
#include "network/net_task.h"
#include "ui/chart_render.h"
#include "ui/crypto_fmt.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace crypto_detail {
namespace {

using namespace theme;

constexpr int kW = 296;  // page content width (320 - 2 * kMargin)
constexpr int kChartW = kW;
constexpr int kChartH = 146;
constexpr int kPriceY = 42;
constexpr int kRangeY = 84;
constexpr int kChartY = 120;
constexpr int kStatsY = kChartY + kChartH + 6;
constexpr int kStatRowH = 17;
constexpr int kStatRows = 5;
// How long a chart stays fresh, per range (the PC app caches with the same periods).
constexpr uint32_t kTtlMs[crypto::kRangeCount] = {30000, 60000, 300000, 900000, 1800000, 3600000, 21600000};

lv_obj_t *s_page = nullptr;
lv_obj_t *s_logo = nullptr;
lv_obj_t *s_name = nullptr;
lv_obj_t *s_sym = nullptr;
lv_obj_t *s_price = nullptr;
lv_obj_t *s_chg = nullptr;
lv_obj_t *s_range_btns = nullptr;
lv_obj_t *s_style_lbl = nullptr;
lv_obj_t *s_box = nullptr;
lv_obj_t *s_canvas = nullptr;
lv_obj_t *s_hi = nullptr;
lv_obj_t *s_lo = nullptr;
lv_obj_t *s_tag = nullptr;
lv_obj_t *s_tag_lbl = nullptr;
lv_obj_t *s_t0 = nullptr;
lv_obj_t *s_t1 = nullptr;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_spin = nullptr;
lv_obj_t *s_vline = nullptr;
lv_obj_t *s_dot = nullptr;
lv_obj_t *s_tip = nullptr;
lv_obj_t *s_stat_val[kStatRows] = {};
const char *s_range_map[crypto::kRangeCount + 1] = {};

uint16_t *s_buf = nullptr;
crypto::Chart *s_chart = nullptr;  // PSRAM copies
crypto::Market *s_m = nullptr;
crypto::Candle *s_disp = nullptr;
float *s_vals = nullptr;
float *s_vols = nullptr;
bool *s_vol_up = nullptr;
int s_disp_n = 0;
float s_vmin = 0, s_vmax = 1;
bool s_candles = false;
chart::Paint s_paint;

int s_coin = -1;
uint8_t s_range = 1;
bool s_have_chart = false;
bool s_have_market = false;
bool s_scrub = false;
bool s_waiting = false;
uint32_t s_req_ms = 0;
uint32_t s_chart_rx = 0;  // lv_tick of the chart shown (for its age)

bool visible() { return current_page() == Page::CryptoDetail; }

const char *pair() { return s_coin >= 0 ? COIN_CATALOG[s_coin].pair : ""; }

const char *wanted_cur() { return cfmt::currency(app::state().settings.crypto_cur).code; }

const crypto::Coin *coin_data() {
  if (!s_have_market || s_coin < 0) return nullptr;
  for (int i = 0; i < s_m->count; i++)
    if (!strcmp(s_m->coins[i].pair, pair())) return &s_m->coins[i];
  return nullptr;
}

const char *money_cur() {
  if (s_have_chart && s_chart->cur[0]) return s_chart->cur;
  return s_have_market ? s_m->cur : wanted_cur();
}

void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (!on && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Local time of a candle, formatted for the range (axis) or with more detail (tooltip).
void fmt_time(uint32_t t, uint8_t range, bool tip, char *out, size_t n) {
  const time_t lt = static_cast<time_t>(t) + static_cast<time_t>(app::state().settings.utc_offset_min) * 60;
  struct tm tm;
  gmtime_r(&lt, &tm);
  char hm[12], dm[24];
  if (app::state().settings.clock_12h) {
    const int h12 = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
    snprintf(hm, sizeof(hm), "%d:%02d%s", h12, tm.tm_min, tm.tm_hour < 12 ? "a" : "p");
  } else {
    snprintf(hm, sizeof(hm), "%02d:%02d", tm.tm_hour, tm.tm_min);
  }
  const char *mon = tr(static_cast<Str>(static_cast<int>(Str::MonJan) + tm.tm_mon));
  if (lang() == storage::Lang::En)
    snprintf(dm, sizeof(dm), "%s %d", mon, tm.tm_mday);
  else
    snprintf(dm, sizeof(dm), "%d %s", tm.tm_mday, mon);
  const auto r = static_cast<crypto::Range>(range);
  if (tip) {
    if (r == crypto::Range::Y1 || r == crypto::Range::All)
      snprintf(out, n, "%s %d", dm, tm.tm_year + 1900);
    else
      snprintf(out, n, "%s  %s", dm, hm);
    return;
  }
  switch (r) {
    case crypto::Range::H1:
    case crypto::Range::H24: snprintf(out, n, "%s", hm); break;
    case crypto::Range::D7: {
      const char *wd = tr(static_cast<Str>(static_cast<int>(Str::WdMon) + (tm.tm_wday + 6) % 7));
      snprintf(out, n, "%s %d", wd, tm.tm_mday);
      break;
    }
    case crypto::Range::M1:
    case crypto::Range::M3: snprintf(out, n, "%s", dm); break;
    default: snprintf(out, n, "%s '%02d", mon, tm.tm_year % 100); break;
  }
}

void set_change(double pct, double abs_v, const char *suffix) {
  char p[24], a[32], b[72];
  cfmt::pct(pct, p, sizeof(p));
  if (crypto::has(abs_v)) {
    cfmt::money(abs_v, cfmt::price_decimals(abs_v), money_cur(), a, sizeof(a));
    snprintf(b, sizeof(b), "%s  %s\n%s%s", p, suffix, abs_v >= 0 ? "+" : "", a);
  } else {
    snprintf(b, sizeof(b), "%s  %s", p, suffix);
  }
  set_text(s_chg, isnan(pct) ? "" : b);
  set_color(s_chg, pct >= 0 ? kUp : kDown);
}

// Header with the live ticker (or the last close when the PC sent no ticker for this coin).
void header_live() {
  const crypto::Coin *cn = coin_data();
  char b[40];
  if (cn && crypto::has(cn->price)) {
    cfmt::money(cn->price, -1, money_cur(), b, sizeof(b));
    set_text(s_price, b);
    set_change(cn->chg_pct, cn->chg_abs, "24h");
  } else if (s_have_chart && s_disp_n > 0) {
    cfmt::money(s_disp[s_disp_n - 1].c, -1, money_cur(), b, sizeof(b));
    set_text(s_price, b);
    set_text(s_chg, "");
  } else {
    set_text(s_price, "--");
    set_text(s_chg, "");
  }
}

void fill_stats() {
  const crypto::Coin *cn = coin_data();
  const char *cur = money_cur();
  char b[40], m[32];
  if (cn) {
    cfmt::money(cn->hi, -1, cur, b, sizeof(b));
    set_text(s_stat_val[0], b);
    cfmt::money(cn->lo, -1, cur, b, sizeof(b));
    set_text(s_stat_val[1], b);
    cfmt::compact_money(cn->vol, cur, b, sizeof(b));
    set_text(s_stat_val[2], b);
    cfmt::compact_money(cn->mcap, cur, m, sizeof(m));
    if (cn->rank > 0 && crypto::has(cn->mcap))
      snprintf(b, sizeof(b), "%s  #%d", m, cn->rank);
    else
      snprintf(b, sizeof(b), "%s", m);
    set_text(s_stat_val[3], b);
    cfmt::money(cn->ath, -1, cur, b, sizeof(b));
    set_text(s_stat_val[4], b);
  } else {
    for (auto *v : s_stat_val) set_text(v, "--");
  }
}

void hide_crosshair() {
  show(s_vline, false);
  show(s_dot, false);
  show(s_tip, false);
}

void set_status(const char *text, bool spinner, bool clickable) {
  set_text(s_status, text ? text : "");
  show(s_status, text != nullptr);
  show(s_spin, spinner);
  if (clickable)
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_CLICKABLE);
  else
    lv_obj_clear_flag(s_status, LV_OBJ_FLAG_CLICKABLE);
}

void show_chart_parts(bool on) {
  show(s_canvas, on);
  show(s_hi, on);
  show(s_lo, on);
  show(s_tag, on);
  show(s_t0, on);
  show(s_t1, on);
}

void render() {
  if (!s_have_chart || !s_buf) return;
  const crypto::Chart &ch = *s_chart;
  s_candles = app::state().settings.crypto_style == 1;
  s_disp_n = s_candles ? chart::aggregate(ch.c, ch.n, s_disp, kChartW / 4)
                       : chart::aggregate(ch.c, ch.n, s_disp, crypto::kMaxCandles);
  const int n = s_disp_n;
  if (n < 2) return;
  float lo = INFINITY, hi = -INFINITY;
  int ilo = 0, ihi = 0;
  for (int i = 0; i < n; i++) {
    if (s_disp[i].l < lo) {
      lo = s_disp[i].l;
      ilo = i;
    }
    if (s_disp[i].h > hi) {
      hi = s_disp[i].h;
      ihi = i;
    }
    s_vals[i] = s_disp[i].c;
    s_vols[i] = s_disp[i].v;
    s_vol_up[i] = s_disp[i].c >= s_disp[i].o;
  }
  (void)ilo;
  (void)ihi;
  if (s_candles) {
    chart::range_candles(s_disp, n, &s_vmin, &s_vmax);
  } else {
    chart::range(s_vals, n, &s_vmin, &s_vmax);
  }
  const float pad = (s_vmax - s_vmin) * 0.04f;
  s_vmin -= pad;
  s_vmax += pad;
  const bool up = s_disp[n - 1].c >= s_disp[0].o;
  chart::Paint &p = s_paint;
  p = chart::Paint{};
  p.bg = kSurface;
  p.line = up ? kUp : kDown;
  p.grid = kBorder;
  p.grid_a = 110;
  p.grid_rows = 4;
  p.line_w = 2.0f;
  p.glow_r = 5.0f;
  p.glow_a = 45;
  p.fill_a = 85;
  p.pad_top = 18;
  p.pad_bottom = 30;
  p.vol_h = 22;
  p.up = kUp;
  p.down = kDown;
  if (s_candles)
    chart::render_candles(s_buf, kChartW, kChartH, s_disp, n, s_vmin, s_vmax, p);
  else
    chart::render_line(s_buf, kChartW, kChartH, s_vals, n, s_vmin, s_vmax, p, s_vols, s_vol_up);
  lv_obj_invalidate(s_canvas);

  char b[48], v[32];
  const char *cur = money_cur();
  cfmt::money(hi, -1, cur, v, sizeof(v));
  snprintf(b, sizeof(b), "%s %s", tr(Str::CryHighShort), v);
  set_text(s_hi, b);
  cfmt::money(lo, -1, cur, v, sizeof(v));
  snprintf(b, sizeof(b), "%s %s", tr(Str::CryLowShort), v);
  set_text(s_lo, b);
  // Last-price tag on the right edge, at the height of the last close.
  cfmt::money(s_disp[n - 1].c, -1, cur, v, sizeof(v));
  set_text(s_tag_lbl, v);
  lv_obj_set_style_bg_color(s_tag, c(p.line), 0);
  int ty = static_cast<int>(chart::value_to_y(s_disp[n - 1].c, s_vmin, s_vmax, kChartH, p)) - 8;
  if (ty < 0) ty = 0;
  if (ty > kChartH - 34) ty = kChartH - 34;
  lv_obj_align(s_tag, LV_ALIGN_TOP_RIGHT, 0, ty);
  fmt_time(s_disp[0].t, s_range, false, b, sizeof(b));
  set_text(s_t0, b);
  fmt_time(s_disp[n - 1].t, s_range, false, b, sizeof(b));
  set_text(s_t1, b);
  show_chart_parts(true);
}

void request_chart() {
  if (s_coin < 0) return;
  net::request_crypto_chart(s_coin, s_range, wanted_cur());
  s_waiting = true;
  s_req_ms = lv_tick_get();
}

// Show the cached chart for (coin, range); ask the PC when there is none or it is stale.
void load_chart(bool may_request) {
  if (s_coin < 0) return;
  uint32_t age = 0;
  bool found = crypto::get_chart(pair(), s_range, wanted_cur(), *s_chart, &age);
  if (!found && strcmp(wanted_cur(), "USD") != 0)  // the PC fell back to USD (no exchange rate)
    found = crypto::get_chart(pair(), s_range, "USD", *s_chart, &age);
  s_have_chart = found && s_chart->ok;
  hide_crosshair();
  s_scrub = false;
  if (s_have_chart) {
    s_chart_rx = lv_tick_get() - age;
    set_status(nullptr, false, false);
    render();
    header_live();
  } else {
    show_chart_parts(false);
    if (found && !s_waiting) {
      set_status(!strcmp(s_chart->error, "pc_app_old") ? tr(Str::CryOldApp) : tr(Str::CryChartFailed), false, true);
    } else if (!app::data_online()) {
      set_status(tr(Str::CryOffline), false, false);
    } else {
      set_status(tr(Str::CryChartLoading), true, false);
    }
  }
  const bool stale = !found || (s_have_chart && age > kTtlMs[s_range]);
  if (may_request && stale && app::data_online()) {
    request_chart();
    if (!s_have_chart) set_status(tr(Str::CryChartLoading), true, false);
  }
}

// Finger on the chart: crosshair + header follow the closest point.
void scrub_at(int x) {
  const int n = s_disp_n;
  if (!s_have_chart || n < 2) return;
  if (x < 0) x = 0;
  if (x > kChartW - 1) x = kChartW - 1;
  int i;
  float px;
  if (s_candles) {
    i = x * n / kChartW;
    if (i > n - 1) i = n - 1;
    px = (i + 0.5f) * kChartW / n;
  } else {
    i = static_cast<int>(lroundf(static_cast<float>(x) * (n - 1) / (kChartW - 1)));
    const float xspan = static_cast<float>(kChartW - 1) - s_paint.line_w;
    px = s_paint.line_w * 0.5f + xspan * i / (n - 1);
  }
  const crypto::Candle &k = s_disp[i];
  const float py = chart::value_to_y(k.c, s_vmin, s_vmax, kChartH, s_paint);
  s_scrub = true;
  show(s_vline, true);
  show(s_dot, true);
  show(s_tip, true);
  lv_obj_set_x(s_vline, static_cast<lv_coord_t>(px));
  lv_obj_set_pos(s_dot, static_cast<lv_coord_t>(px - 4), static_cast<lv_coord_t>(py - 4));
  char b[40];
  fmt_time(k.t, s_range, true, b, sizeof(b));
  set_text(s_tip, b);
  lv_obj_update_layout(s_tip);
  const lv_coord_t tw = lv_obj_get_width(s_tip);
  lv_coord_t tx = static_cast<lv_coord_t>(px) - tw / 2;
  if (tx < 2) tx = 2;
  if (tx > kChartW - tw - 2) tx = kChartW - tw - 2;
  lv_obj_set_pos(s_tip, tx, 2);
  cfmt::money(k.c, -1, money_cur(), b, sizeof(b));
  set_text(s_price, b);
  const float base = s_disp[0].o;
  if (base > 0) set_change((k.c - base) / base * 100.0, k.c - base, tr(static_cast<Str>(static_cast<int>(Str::R1H) + s_range)));
}

void chart_touch_cb(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return;
    lv_point_t pt;
    lv_indev_get_point(in, &pt);
    lv_area_t a;
    lv_obj_get_coords(s_canvas, &a);
    scrub_at(pt.x - a.x1);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    s_scrub = false;
    hide_crosshair();
    header_live();
  }
}

void range_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel >= crypto::kRangeCount || sel == s_range) return;
  s_range = static_cast<uint8_t>(sel);
  app::set_crypto_range(s_range);
  s_waiting = false;
  load_chart(true);
}

void refresh_style_btn() {
  // The button shows the style you switch to.
  set_text(s_style_lbl, app::state().settings.crypto_style == 1 ? PCD_SYM_CHART_LINE : PCD_SYM_CHART_BAR);
}

void style_cb(lv_event_t *) {
  app::set_crypto_style(app::state().settings.crypto_style == 1 ? 0 : 1);
  refresh_style_btn();
  render();
}

void go_coin(int delta) {
  int8_t idx[crypto::kMaxCoins];
  const int n = crypto::parse_watch(app::state().settings.crypto_coins, idx, crypto::kMaxCoins);
  if (n < 2) return;
  int pos = 0;
  for (int i = 0; i < n; i++)
    if (idx[i] == s_coin) pos = i;
  ui::crypto_detail::show(idx[(pos + delta + n) % n]);
}

void gesture_cb(lv_event_t *) {
  lv_indev_t *in = lv_indev_get_act();
  if (!in || s_scrub) return;
  const lv_dir_t d = lv_indev_get_gesture_dir(in);
  if (d == LV_DIR_LEFT) go_coin(+1);
  if (d == LV_DIR_RIGHT) go_coin(-1);
}

lv_obj_t *small_button(lv_obj_t *parent, const char *sym, int x, int y, int w, int h, lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_bg_color(b, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(b, 4);
  lv_obj_t *l = make_label(b, font_small(), kText, sym);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  if (!s_chart) {
    void *a = heap_caps_malloc(sizeof(crypto::Chart), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *b = heap_caps_malloc(sizeof(crypto::Market), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_disp = static_cast<crypto::Candle *>(heap_caps_malloc(sizeof(crypto::Candle) * crypto::kMaxCandles, MALLOC_CAP_SPIRAM));
    s_vals = static_cast<float *>(heap_caps_malloc(sizeof(float) * crypto::kMaxCandles, MALLOC_CAP_SPIRAM));
    s_vols = static_cast<float *>(heap_caps_malloc(sizeof(float) * crypto::kMaxCandles, MALLOC_CAP_SPIRAM));
    s_vol_up = static_cast<bool *>(heap_caps_malloc(sizeof(bool) * crypto::kMaxCandles, MALLOC_CAP_SPIRAM));
    s_buf = static_cast<uint16_t *>(heap_caps_malloc(kChartW * kChartH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (a) {
      s_chart = new (a) crypto::Chart;
      crypto::reset(*s_chart);
    }
    if (b) {
      s_m = new (b) crypto::Market;
      crypto::reset(*s_m);
    }
  }
  s_have_chart = false;
  s_range = app::state().settings.crypto_range < crypto::kRangeCount ? app::state().settings.crypto_range : 1;

  s_page = make_page(parent, false);
  lv_obj_set_layout(s_page, 0);  // children are positioned by hand (296 x 358 inside the padding)
  lv_obj_set_style_pad_top(s_page, 6, 0);
  lv_obj_set_style_pad_bottom(s_page, 6, 0);
  lv_obj_add_event_cb(s_page, gesture_cb, LV_EVENT_GESTURE, nullptr);

  // Header: back to the list, logo, name / symbol; previous / next coin.
  small_button(s_page, LV_SYMBOL_LEFT, 0, 0, 40, 36, [](lv_event_t *) { navigate(Page::Crypto); });
  s_logo = lv_img_create(s_page);
  lv_obj_set_pos(s_logo, 48, 2);
  s_name = make_label(s_page, font_body(), kText, "");
  lv_obj_set_width(s_name, kW - 88 - 84);
  lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_name, 88, -1);
  s_sym = make_label(s_page, font_tiny(), kMuted, "");
  lv_obj_set_pos(s_sym, 88, 20);
  small_button(s_page, LV_SYMBOL_UP, kW - 82, 0, 38, 36, [](lv_event_t *) { go_coin(-1); });
  small_button(s_page, LV_SYMBOL_DOWN, kW - 38, 0, 38, 36, [](lv_event_t *) { go_coin(+1); });

  // Price row: live price (or the point under the finger) and its change.
  s_price = make_label(s_page, font_value(), kText, "--");
  lv_obj_set_width(s_price, kW - 120);
  lv_label_set_long_mode(s_price, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(s_price, 0, kPriceY);
  s_chg = make_label(s_page, font_tiny(), kMuted, "");
  lv_obj_set_size(s_chg, 116, 2 * lv_font_get_line_height(font_tiny()));
  lv_label_set_long_mode(s_chg, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_chg, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(s_chg, kW - 116, kPriceY + 3);

  // Range selector + line/candles toggle.
  for (int i = 0; i < crypto::kRangeCount; i++) s_range_map[i] = tr(static_cast<Str>(static_cast<int>(Str::R1H) + i));
  s_range_map[crypto::kRangeCount] = "";
  lv_obj_t *rm = lv_btnmatrix_create(s_page);
  lv_btnmatrix_set_map(rm, s_range_map);
  lv_btnmatrix_set_btn_ctrl_all(rm, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(rm, true);
  lv_btnmatrix_set_btn_ctrl(rm, s_range, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_pos(rm, 0, kRangeY);
  lv_obj_set_size(rm, kW - 52, 30);
  lv_obj_set_style_pad_all(rm, 2, 0);
  lv_obj_set_style_pad_gap(rm, 2, 0);
  lv_obj_set_style_bg_color(rm, c(kSurface), 0);
  lv_obj_set_style_border_width(rm, 0, 0);
  lv_obj_set_style_radius(rm, 8, 0);
  lv_obj_set_style_bg_opa(rm, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(rm, c(kBorder), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(rm, LV_OPA_COVER, static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(rm, c(kMuted), LV_PART_ITEMS);
  lv_obj_set_style_text_color(rm, c(kText), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_obj_set_style_text_font(rm, font_tiny(), LV_PART_ITEMS);
  lv_obj_set_style_radius(rm, 6, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(rm, 0, LV_PART_ITEMS);
  lv_obj_set_style_border_width(rm, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(rm, range_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  s_range_btns = rm;
  lv_obj_t *sb = small_button(s_page, "", kW - 46, kRangeY, 46, 30, style_cb);
  s_style_lbl = lv_obj_get_child(sb, 0);
  refresh_style_btn();

  // Chart box: canvas + overlays (labels, crosshair, status).
  s_box = lv_obj_create(s_page);
  lv_obj_remove_style_all(s_box);
  lv_obj_set_pos(s_box, 0, kChartY);
  lv_obj_set_size(s_box, kChartW, kChartH);
  lv_obj_set_style_bg_color(s_box, c(kSurface), 0);
  lv_obj_set_style_bg_opa(s_box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_box, 8, 0);
  lv_obj_set_style_clip_corner(s_box, true, 0);
  lv_obj_clear_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
  s_canvas = lv_canvas_create(s_box);
  if (s_buf) {
    lv_canvas_set_buffer(s_canvas, s_buf, kChartW, kChartH, LV_IMG_CF_TRUE_COLOR);
    chart::clear(s_buf, kChartW, kChartH, kSurface);
  }
  lv_obj_set_pos(s_canvas, 0, 0);
  lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(s_canvas, chart_touch_cb, LV_EVENT_ALL, nullptr);
  s_hi = make_label(s_box, font_tiny(), kMuted, "");
  lv_obj_set_pos(s_hi, 6, 2);
  s_lo = make_label(s_box, font_tiny(), kMuted, "");
  lv_obj_align(s_lo, LV_ALIGN_BOTTOM_LEFT, 6, -14);
  s_t0 = make_label(s_box, font_tiny(), kMuted, "");
  lv_obj_align(s_t0, LV_ALIGN_BOTTOM_LEFT, 6, 1);
  s_t1 = make_label(s_box, font_tiny(), kMuted, "");
  lv_obj_align(s_t1, LV_ALIGN_BOTTOM_RIGHT, -6, 1);
  s_tag = lv_obj_create(s_box);
  lv_obj_remove_style_all(s_tag);
  lv_obj_set_size(s_tag, LV_SIZE_CONTENT, 16);
  lv_obj_set_style_radius(s_tag, 4, 0);
  lv_obj_set_style_bg_opa(s_tag, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(s_tag, 4, 0);
  lv_obj_clear_flag(s_tag, LV_OBJ_FLAG_CLICKABLE);
  s_tag_lbl = make_label(s_tag, font_tiny(), kBg, "");
  lv_obj_center(s_tag_lbl);
  for (lv_obj_t *o : {s_hi, s_lo, s_t0, s_t1}) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  // Crosshair
  s_vline = lv_obj_create(s_box);
  lv_obj_remove_style_all(s_vline);
  lv_obj_set_size(s_vline, 1, kChartH);
  lv_obj_set_style_bg_color(s_vline, c(kText), 0);
  lv_obj_set_style_bg_opa(s_vline, LV_OPA_40, 0);
  lv_obj_clear_flag(s_vline, LV_OBJ_FLAG_CLICKABLE);
  s_dot = lv_obj_create(s_box);
  lv_obj_remove_style_all(s_dot);
  lv_obj_set_size(s_dot, 9, 9);
  lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(s_dot, c(kText), 0);
  lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_dot, c(kBg), 0);
  lv_obj_set_style_border_width(s_dot, 2, 0);
  lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
  s_tip = make_label(s_box, font_tiny(), kText, "");
  lv_obj_set_style_bg_color(s_tip, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(s_tip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_tip, 4, 0);
  lv_obj_set_style_pad_hor(s_tip, 5, 0);
  lv_obj_set_style_pad_ver(s_tip, 1, 0);
  lv_obj_clear_flag(s_tip, LV_OBJ_FLAG_CLICKABLE);
  hide_crosshair();
  // Status (loading / error, tap to retry)
  s_spin = lv_spinner_create(s_box, 1000, 60);
  lv_obj_set_size(s_spin, 26, 26);
  lv_obj_set_style_arc_width(s_spin, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_spin, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_spin, c(kCrypto), LV_PART_INDICATOR);
  lv_obj_align(s_spin, LV_ALIGN_CENTER, 0, -14);
  s_status = make_label(s_box, font_small(), kMuted, "");
  lv_obj_set_width(s_status, kChartW - 24);
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 16);
  lv_obj_add_event_cb(
      s_status,
      [](lv_event_t *) {
        if (!app::data_online()) return;
        request_chart();
        set_status(tr(Str::CryChartLoading), true, false);
      },
      LV_EVENT_CLICKED, nullptr);
  show_chart_parts(false);
  set_status(nullptr, false, false);

  // Stats card under the chart: caption left, value right.
  lv_obj_t *card = make_card(s_page);
  lv_obj_set_pos(card, 0, kStatsY);
  lv_obj_set_size(card, kW, kStatRows * kStatRowH + 8);
  lv_obj_set_style_pad_ver(card, 4, 0);
  lv_obj_set_style_pad_hor(card, 10, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
  const Str caps[kStatRows] = {Str::CryHigh24, Str::CryLow24, Str::CryVol24, Str::CryMcap, Str::CryAth};
  for (int i = 0; i < kStatRows; i++) {
    lv_obj_t *cap = make_label(card, font_tiny(), kMuted, tr(caps[i]));
    lv_obj_set_pos(cap, 0, i * kStatRowH + 1);
    s_stat_val[i] = make_label(card, font_small(), kText, "--");
    lv_obj_set_width(s_stat_val[i], 170);
    lv_label_set_long_mode(s_stat_val[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_stat_val[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_stat_val[i], LV_ALIGN_TOP_RIGHT, 0, i * kStatRowH - 1);
  }
  return s_page;
}

void show(int coin) {
  if (coin < 0 || coin >= COIN_CATALOG_COUNT || !s_page) return;
  const bool same = coin == s_coin;
  s_coin = coin;
  if (!same) s_waiting = false;
  if (!visible()) navigate(Page::CryptoDetail);
  const coin_info_t &ci = COIN_CATALOG[coin];
  set_text(s_name, ci.name);
  set_text(s_sym, ci.sym);
  lv_img_set_src(s_logo, ci.logo_m);
  refresh_style_btn();  // the style may have changed in Settings
  s_have_market = s_m && crypto::get_market(*s_m, nullptr);
  header_live();
  fill_stats();
  load_chart(true);
}

void on_data() {
  if (!s_page || !s_m) return;
  s_have_market = crypto::get_market(*s_m, nullptr);
  if (!visible()) return;
  if (!s_scrub) header_live();
  fill_stats();
}

void on_chart() {
  if (!s_page || s_coin < 0) return;
  s_waiting = false;
  if (visible()) load_chart(false);
}

void tick() {
  if (!s_page || !visible() || s_coin < 0) return;
  const uint32_t now = lv_tick_get();
  if (s_waiting && now - s_req_ms > 12000) s_waiting = false;
  // Keep the chart fresh while it is on screen (the PC answers from its cache when it can).
  if (!s_waiting && s_have_chart && now - s_chart_rx > kTtlMs[s_range] && !s_scrub &&
      app::data_online())
    request_chart();
}

}  // namespace crypto_detail
}  // namespace ui
