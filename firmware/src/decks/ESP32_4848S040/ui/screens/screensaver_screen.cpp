// Screensaver: a full-screen wallpaper (fixed, random at every start, or rotating with a slow
// crossfade), a big clock with a blinking colon, the long date, the weather, and glass cards with the
// data the user picked in Settings (PC, now playing, crypto). It lives on the top layer above every
// page; a touch (swallowed: it never presses what is underneath) or the BOOT button closes it.
// While it is enabled it replaces auto sleep (app_controller): the deck never sleeps by itself.
// Anti burn-in: the content drifts a few pixels every minute and the colon blinks.
//
// Portrait 320 x 480: clock, date and weather stacked in the calm upper half of the wallpaper, the
// cards stacked full width at the bottom (the ones without data are hidden, the stack shrinks
// downwards).
//
// The cards and the weather line are shortcuts: a tap closes the screensaver and opens the matching
// page (PC -> Monitor, music -> Player, crypto -> the coin on the card, weather -> Today). A finger
// that slides off just closes it, like a tap anywhere else.
//
// Also the wallpaper picker sheet opened from Settings › Screensaver › Image.

#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "assets/wallpapers.h"
#include "board/backlight.h"
#include "crypto/coin_catalog.h"
#include "crypto/crypto_store.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "ui/crypto_fmt.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"
#include "ui/wallpaper.h"

namespace ui {
namespace screensaver {
namespace {

using namespace theme;

// Layout (320 x 480 portrait).
constexpr int kEdge = 16;                     // content inset from the screen edge (before drift)
constexpr int kDrift = 8;                     // max drift each way, px (stays inside the screen)
constexpr int kContentW = kScreenW - 2 * kEdge;  // 288
constexpr int kContentH = kScreenH - 2 * kEdge;  // 448
constexpr int kClockY = 26;                   // clock top, inside the content box
constexpr int kCardW = kContentW;             // cards stacked, full width
constexpr int kCardH = 64;
constexpr int kCardPad = 10;
constexpr int kCardThumb = 44;                // cover / logo box inside a card
constexpr int kCardGap = 8;
// Timing.
constexpr uint32_t kFadeMs = 700;        // appearance
constexpr uint32_t kCrossfadeMs = 1600;  // wallpaper change
constexpr uint32_t kCoinMs = 8000;       // crypto card: next coin
constexpr uint32_t kDriftMs = 60000;
constexpr uint32_t kHintMs = 3500;       // "touch to return" hint

constexpr uint32_t kGlass = 0x070B14;

struct Card {
  lv_obj_t *root = nullptr;
  lv_obj_t *box = nullptr;  // left square: icon, cover or logo
  lv_obj_t *icon = nullptr;
  lv_obj_t *img = nullptr;
  lv_obj_t *line1 = nullptr;
  lv_obj_t *line2 = nullptr;
};

lv_obj_t *s_root = nullptr;
lv_obj_t *s_img[2] = {};
wallpaper::Image s_wp[2];
int s_front = 0;         // which s_img is the visible one
int s_cycle_next = -1;   // next wallpaper of the rotation (kept across sessions)
lv_obj_t *s_content = nullptr;
lv_obj_t *s_clock = nullptr, *s_hh = nullptr, *s_colon = nullptr, *s_mm = nullptr, *s_ampm = nullptr;
lv_obj_t *s_date = nullptr;
lv_obj_t *s_wx = nullptr, *s_wx_icon = nullptr, *s_wx_temp = nullptr, *s_wx_text = nullptr;
lv_obj_t *s_cards = nullptr;
lv_obj_t *s_hint = nullptr;
Card s_pc, s_media, s_coin;
const void *s_cover_src = nullptr;
uint32_t s_bg_ms = 0, s_coin_ms = 0, s_drift_ms = 0;
int s_coin_pos = 0;
int s_coin_cat = -1;  // COIN_CATALOG index of the coin on the crypto card (-1: unknown pair)
int s_last_min = -1;
bool s_colon_on = true;
crypto::Market *s_m = nullptr;

// ---- helpers ------------------------------------------------------------------------------------
lv_obj_t *box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, int gap) {
  lv_obj_t *r = box(parent);
  lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, flow);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (flow == LV_FLEX_FLOW_ROW)
    lv_obj_set_style_pad_column(r, gap, 0);
  else
    lv_obj_set_style_pad_row(r, gap, 0);
  return r;
}

void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (!on && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ---- shortcuts ----------------------------------------------------------------------------------
enum Shortcut : intptr_t { kToMonitor, kToPlayer, kToCrypto, kToToday };

// A tap on a card / the weather line: close, then open its page. PRESS_LOST, or a release away from
// the card (objects keep the press when the finger slides off — PRESS_LOCK — and still get CLICKED):
// only close. Either way the touch never reaches the page underneath.
void shortcut_cb(lv_event_t *e) {
  if (!s_root) return;  // already closed by this gesture
  bool tap = lv_event_get_code(e) == LV_EVENT_CLICKED;
  if (tap) {
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    if (indev) {
      lv_indev_get_point(indev, &p);
      tap = lv_obj_hit_test(lv_event_get_current_target(e), &p);
    }
  }
  const auto to = static_cast<Shortcut>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const int coin = s_coin_cat;
  stop();
  if (!tap) return;
  switch (to) {
    case kToMonitor: navigate(Page::Home); break;
    case kToPlayer: navigate(Page::Controls); break;
    case kToToday: navigate(Page::Today); break;
    case kToCrypto:
      if (coin >= 0 && coin < COIN_CATALOG_COUNT)
        crypto_detail::show(coin);  // opens the coin's detail page
      else
        navigate(Page::Crypto);
      break;
  }
}

void make_shortcut(lv_obj_t *o, Shortcut to) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, shortcut_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(to));
  lv_obj_add_event_cb(o, shortcut_cb, LV_EVENT_PRESS_LOST, reinterpret_cast<void *>(to));
}

// Frosted-glass card, full width: dark translucent fill, hairline light border; a square on the left
// (icon, cover or coin logo) and two lines of text.
Card make_card(lv_obj_t *parent) {
  Card k;
  k.root = box(parent);
  lv_obj_set_size(k.root, kCardW, kCardH);
  lv_obj_set_style_radius(k.root, 18, 0);
  lv_obj_set_style_bg_color(k.root, c(kGlass), 0);
  lv_obj_set_style_bg_opa(k.root, LV_OPA_60, 0);
  lv_obj_set_style_border_color(k.root, lv_color_white(), 0);
  lv_obj_set_style_border_opa(k.root, LV_OPA_20, 0);
  lv_obj_set_style_border_width(k.root, 1, 0);
  lv_obj_set_style_pad_all(k.root, kCardPad, 0);
  // Pressed (it is a shortcut): a lighter, denser glass and a brighter edge.
  lv_obj_set_style_bg_color(k.root, c(0x1C2638), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(k.root, LV_OPA_80, LV_STATE_PRESSED);
  lv_obj_set_style_border_opa(k.root, LV_OPA_50, LV_STATE_PRESSED);
  k.box = box(k.root);
  lv_obj_set_size(k.box, kCardThumb, kCardThumb);
  lv_obj_align(k.box, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(k.box, 12, 0);
  lv_obj_set_style_clip_corner(k.box, true, 0);
  lv_obj_set_style_bg_color(k.box, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(k.box, LV_OPA_10, 0);
  k.icon = make_label(k.box, font_value(), kText, "");
  lv_obj_center(k.icon);
  k.img = lv_img_create(k.box);
  lv_obj_center(k.img);
  lv_obj_add_flag(k.img, LV_OBJ_FLAG_HIDDEN);
  const int tx = kCardThumb + 12, tw = kCardW - 2 * kCardPad - tx;  // 212 px of text
  k.line1 = make_label(k.root, font_body(), kText, "");
  lv_obj_set_size(k.line1, tw, lv_font_get_line_height(font_body()));
  lv_label_set_long_mode(k.line1, LV_LABEL_LONG_DOT);
  lv_obj_align(k.line1, LV_ALIGN_LEFT_MID, tx, -10);
  k.line2 = make_label(k.root, font_small(), kMuted, "");
  lv_obj_set_size(k.line2, tw, lv_font_get_line_height(font_small()));
  lv_label_set_long_mode(k.line2, LV_LABEL_LONG_DOT);
  lv_obj_align(k.line2, LV_ALIGN_LEFT_MID, tx, 12);
  return k;
}

void fade(lv_obj_t *o, uint32_t ms, uint32_t delay, bool in, lv_anim_ready_cb_t ready = nullptr) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_values(&a, in ? LV_OPA_TRANSP : LV_OPA_COVER, in ? LV_OPA_COVER : LV_OPA_TRANSP);
  lv_anim_set_time(&a, ms);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) { lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), v, 0); });
  if (ready) lv_anim_set_ready_cb(&a, ready);
  lv_anim_start(&a);
}

int pick_start_wallpaper() {
  const storage::Screensaver &ss = app::state().settings.ss;
  const int n = wallpaper::count();
  if (n <= 0) return -1;
  switch (static_cast<storage::SsBg>(ss.bg_mode)) {
    case storage::SsBg::Random: {
      int idx = static_cast<int>(esp_random() % n);
      if (n > 1 && idx == s_wp[s_front].idx) idx = (idx + 1) % n;  // never the same twice in a row
      return idx;
    }
    case storage::SsBg::Cycle:
      if (s_cycle_next < 0 || s_cycle_next >= n) s_cycle_next = ss.bg < n ? ss.bg : 0;
      return s_cycle_next;
    default:
      return ss.bg < n ? ss.bg : 0;
  }
}

void set_wallpaper(int slot, int idx) {
  if (idx < 0 || !wallpaper::decode(idx, s_wp[slot])) {
    lv_obj_add_flag(s_img[slot], LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_img_set_src(s_img[slot], &s_wp[slot].dsc);
  lv_obj_clear_flag(s_img[slot], LV_OBJ_FLAG_HIDDEN);
  if (static_cast<storage::SsBg>(app::state().settings.ss.bg_mode) == storage::SsBg::Cycle)
    s_cycle_next = (idx + 1) % wallpaper::count();
}

// Rotation: decode the next image into the hidden slot, put it on top and fade it in.
void next_wallpaper() {
  const int back = 1 - s_front;
  const int idx = pick_start_wallpaper();
  if (idx < 0 || idx == s_wp[s_front].idx) return;
  set_wallpaper(back, idx);
  if (lv_obj_has_flag(s_img[back], LV_OBJ_FLAG_HIDDEN)) return;
  lv_obj_move_to_index(s_img[back], 1);  // above the current image, below the shade and content
  lv_obj_set_style_opa(s_img[back], LV_OPA_TRANSP, 0);
  s_front = back;
  fade(s_img[back], kCrossfadeMs, 0, true, [](lv_anim_t *) {
    if (s_root) lv_obj_add_flag(s_img[1 - s_front], LV_OBJ_FLAG_HIDDEN);
  });
}

void drift() {
  const int dx = static_cast<int>(esp_random() % (2 * kDrift + 1)) - kDrift;
  const int dy = static_cast<int>(esp_random() % (2 * kDrift + 1)) - kDrift;
  lv_obj_set_pos(s_content, kEdge + dx, kEdge + dy);
}

// ---- content --------------------------------------------------------------------------------------
void update_clock() {
  struct tm t;
  const auto &cfg = app::state().settings;
  if (!app::local_time(t)) {
    set_text(s_hh, "--");
    set_text(s_mm, "--");
    set_text(s_ampm, "");
    set_text(s_date, "");
    s_last_min = -1;
    return;
  }
  const int stamp = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min + (cfg.clock_12h ? 100000 : 0);
  if (stamp == s_last_min) return;
  s_last_min = stamp;
  char b[64];
  if (cfg.clock_12h) {
    snprintf(b, sizeof(b), "%d", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12);
    set_text(s_ampm, t.tm_hour < 12 ? "AM" : "PM");
  } else {
    snprintf(b, sizeof(b), "%02d", t.tm_hour);
    set_text(s_ampm, "");
  }
  set_text(s_hh, b);
  snprintf(b, sizeof(b), "%02d", t.tm_min);
  set_text(s_mm, b);
  show(s_ampm, cfg.clock_12h);
  today::format_date(t, b, sizeof(b));
  set_text(s_date, b);
}

void update_weather() {
  const auto &st = app::state();
  const telemetry::Weather &w = st.weather;
  const bool on = (st.settings.ss.show & storage::kSsWeather) && w.ok && st.settings.place_name[0];
  show(s_wx, on);
  if (!on) return;
  set_text(s_wx_icon, today::weather_icon(w.code, w.day));
  set_color(s_wx_icon, today::weather_color(w.code, w.day));
  char b[80];
  fmt_deg(b, sizeof(b), w.temp);
  set_text(s_wx_temp, b);
  snprintf(b, sizeof(b), "%s  ·  %s", today::weather_text(w.code), st.settings.place_name);
  set_text(s_wx_text, b);
}

void pc_line(char *out, size_t n, const char *name, float temp, float load) {
  char t[16] = "", l[16] = "";
  if (telemetry::has(temp)) snprintf(t, sizeof(t), "%.0f°", static_cast<double>(temp));
  if (telemetry::has(load)) snprintf(l, sizeof(l), "%.0f %%", static_cast<double>(load));
  snprintf(out, n, "%s  %s%s%s", name, t[0] ? t : "--", l[0] ? "   " : "", l);
}

bool update_pc() {
  const auto &st = app::state();
  const bool on = (st.settings.ss.show & storage::kSsPc) && st.has_snapshot && st.link == app::LinkState::Online;
  show(s_pc.root, on);
  if (!on) return false;
  char b[48];
  pc_line(b, sizeof(b), "CPU", st.snap.cpu.temp_c, st.snap.cpu.load_pct);
  set_text(s_pc.line1, b);
  pc_line(b, sizeof(b), "GPU", st.snap.gpu.temp_c, st.snap.gpu.load_pct);
  set_text(s_pc.line2, b);
  return true;
}

bool update_media() {
  const auto &st = app::state();
  const auto &s = st.snap;
  const bool pc = st.link == app::LinkState::Online || st.link == app::LinkState::Stale;
  const bool on = (st.settings.ss.show & storage::kSsMedia) && pc && st.has_snapshot && s.has_media && s.media_title[0];
  show(s_media.root, on);
  if (!on) return false;
  set_text(s_media.line1, s.media_title);
  set_text(s_media.line2, s.media_artist[0] ? s.media_artist : s.media_app);
  const lv_img_dsc_t *cover = controls::cover();
  if (cover != s_cover_src) {
    s_cover_src = cover;
    if (cover) {
      lv_img_set_src(s_media.img, cover);
      const int side = cover->header.w > cover->header.h ? cover->header.w : cover->header.h;
      lv_img_set_zoom(s_media.img, static_cast<uint16_t>((256 * kCardThumb + side / 2) / (side ? side : 1)));
      lv_obj_center(s_media.img);
    }
  }
  show(s_media.img, cover != nullptr);
  show(s_media.icon, cover == nullptr);
  set_text(s_media.icon, s.media_playing ? LV_SYMBOL_AUDIO : LV_SYMBOL_PAUSE);
  return true;
}

bool update_coin(bool advance) {
  const auto &st = app::state();
  bool have = false;
  if ((st.settings.ss.show & storage::kSsCrypto) && s_m) {
    uint32_t rx = 0;
    have = crypto::get_market(*s_m, &rx) && s_m->ok && s_m->count > 0;
  }
  show(s_coin.root, have);
  if (!have) return false;
  if (advance) s_coin_pos++;
  if (s_coin_pos >= s_m->count) s_coin_pos = 0;
  const crypto::Coin &cn = s_m->coins[s_coin_pos];
  int cat = -1;
  for (int i = 0; i < COIN_CATALOG_COUNT; i++)
    if (!strcmp(COIN_CATALOG[i].pair, cn.pair)) cat = i;
  s_coin_cat = cat;  // a tap on the card opens this coin
  char b[48], p[40];
  if (cat >= 0) {
    lv_img_set_src(s_coin.img, COIN_CATALOG[cat].logo_m);
    lv_obj_center(s_coin.img);
  }
  show(s_coin.img, cat >= 0);
  show(s_coin.icon, cat < 0);
  // Price on the main line; symbol and 24 h change (coloured) below.
  cfmt::money(cn.price, -1, s_m->cur, p, sizeof(p));
  set_text(s_coin.line1, p);
  const char *sym = cat >= 0 ? COIN_CATALOG[cat].sym : cn.pair;
  if (!isnan(cn.chg_pct)) {
    cfmt::pct(cn.chg_pct, p, sizeof(p));
    snprintf(b, sizeof(b), "%s   %s %s", sym, cn.chg_pct >= 0 ? PCD_SYM_CARET_UP : PCD_SYM_CARET_DOWN, p);
    set_color(s_coin.line2, cn.chg_pct >= 0 ? kUp : kDown);
  } else {
    snprintf(b, sizeof(b), "%s", sym);
    set_color(s_coin.line2, kMuted);
  }
  set_text(s_coin.line2, b);
  return true;
}

void update_data() {
  update_weather();
  const bool a = update_pc();
  const bool b = update_media();
  const bool c = update_coin(false);
  show(s_cards, a || b || c);
}

void close_cb(lv_event_t *) { stop(); }

void build() {
  s_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, kScreenW, kScreenH);
  lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);  // swallows the touch that closes it
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_root, close_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(s_root, close_cb, LV_EVENT_PRESS_LOST, nullptr);

  for (auto &im : s_img) {
    im = lv_img_create(s_root);
    lv_obj_set_pos(im, 0, 0);
    lv_obj_clear_flag(im, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(im, LV_OBJ_FLAG_HIDDEN);
  }
  // A light overall shade keeps white type readable on the brightest images.
  lv_obj_t *shade = box(s_root);
  lv_obj_set_size(shade, kScreenW, kScreenH);
  lv_obj_set_style_bg_color(shade, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(shade, LV_OPA_20, 0);

  s_content = box(s_root);
  lv_obj_set_size(s_content, kContentW, kContentH);
  lv_obj_set_pos(s_content, kEdge, kEdge);

  // Clock: HH : MM (colon blinks) + AM/PM, centred in the calm upper band of the wallpapers.
  s_clock = flex(s_content, LV_FLEX_FLOW_ROW, 4);
  lv_obj_set_flex_align(s_clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, kClockY);
  s_hh = make_label(s_clock, font_clock_xl(), kText, "--");
  s_colon = make_label(s_clock, font_clock_xl(), kText, ":");
  lv_obj_set_style_translate_y(s_colon, -5, 0);
  s_mm = make_label(s_clock, font_clock_xl(), kText, "--");
  s_ampm = make_label(s_clock, font_title(), kText, "");
  lv_obj_set_style_pad_bottom(s_ampm, 9, 0);
  lv_obj_set_style_pad_left(s_ampm, 4, 0);
  const int clock_h = lv_font_get_line_height(font_clock_xl());
  s_date = make_label(s_content, font_title(), kText, "");
  lv_obj_set_width(s_date, kContentW);
  lv_label_set_long_mode(s_date, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_opa(s_date, LV_OPA_90, 0);
  lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, kClockY + clock_h + 6);
  // Weather: icon + temperature, then condition · place (its own line: the screen is narrow). A
  // shortcut to Today: the padding makes room for its pressed highlight.
  constexpr int kWxPad = 6;
  s_wx = flex(s_content, LV_FLEX_FLOW_COLUMN, 0);
  lv_obj_set_style_pad_ver(s_wx, kWxPad, 0);
  lv_obj_set_style_radius(s_wx, 16, 0);
  lv_obj_set_style_bg_color(s_wx, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(s_wx, LV_OPA_10, LV_STATE_PRESSED);
  lv_obj_align(s_wx, LV_ALIGN_TOP_MID, 0,
               kClockY + clock_h + 6 + lv_font_get_line_height(font_title()) + 18 - kWxPad);
  make_shortcut(s_wx, kToToday);
  lv_obj_t *wx_row = flex(s_wx, LV_FLEX_FLOW_ROW, 10);
  s_wx_icon = make_label(wx_row, font_value(), kText, "");
  s_wx_temp = make_label(wx_row, font_value(), kText, "");
  s_wx_text = make_label(s_wx, font_body(), kText, "");
  lv_obj_set_width(s_wx_text, kContentW);
  lv_label_set_long_mode(s_wx_text, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_wx_text, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_opa(s_wx_text, LV_OPA_80, 0);

  // Cards stacked at the bottom (only the ones with something to show; the stack stays at the bottom).
  s_cards = flex(s_content, LV_FLEX_FLOW_COLUMN, kCardGap);
  lv_obj_align(s_cards, LV_ALIGN_BOTTOM_MID, 0, 0);
  s_pc = make_card(s_cards);
  set_text(s_pc.icon, PCD_SYM_GAUGE);
  set_color(s_pc.icon, kCpu);
  make_shortcut(s_pc.root, kToMonitor);
  s_media = make_card(s_cards);
  set_color(s_media.icon, kGpu);
  make_shortcut(s_media.root, kToPlayer);
  lv_label_set_long_mode(s_media.line1, LV_LABEL_LONG_SCROLL_CIRCULAR);  // long titles glide by
  lv_obj_set_style_anim_speed(s_media.line1, 24, 0);  // px/s: slow, and fewer full-frame pushes
  s_coin = make_card(s_cards);
  set_text(s_coin.icon, PCD_SYM_BITCOIN);
  set_color(s_coin.icon, kCrypto);
  lv_obj_set_style_bg_opa(s_coin.box, LV_OPA_TRANSP, 0);  // round logos sit on the glass directly
  make_shortcut(s_coin.root, kToCrypto);
  s_cover_src = nullptr;
  s_coin_cat = -1;

  s_hint = make_label(s_root, font_small(), kText, tr(Str::SsTapToClose));
  lv_obj_set_style_text_opa(s_hint, LV_OPA_70, 0);
  lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 8);
}

}  // namespace

bool active() { return s_root != nullptr; }

void start() {
  if (s_root) return;
  if (!s_m) {
    void *mem = heap_caps_malloc(sizeof(crypto::Market), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem) {
      s_m = new (mem) crypto::Market;
      crypto::reset(*s_m);
    }
  }
  build();
  s_front = 0;
  set_wallpaper(0, pick_start_wallpaper());
  lv_obj_set_style_opa(s_img[0], LV_OPA_TRANSP, 0);
  fade(s_img[0], kFadeMs, 0, true);
  lv_obj_set_style_opa(s_content, LV_OPA_TRANSP, 0);
  fade(s_content, kFadeMs, 250, true);
  fade(s_hint, 600, kHintMs, false);
  s_last_min = -1;
  s_colon_on = true;
  s_coin_pos = 0;
  const uint32_t now = lv_tick_get();
  s_bg_ms = s_coin_ms = s_drift_ms = now;
  update_clock();
  update_data();
  const auto &cfg = app::state().settings;
  const int b = cfg.brightness * cfg.ss.dim / 100;
  backlight::set(static_cast<uint8_t>(b < 5 ? 5 : b));
}

void stop() {
  if (!s_root) return;
  lv_anim_del(s_img[0], nullptr);
  lv_anim_del(s_img[1], nullptr);
  lv_anim_del(s_content, nullptr);
  lv_anim_del(s_hint, nullptr);
  lv_obj_del_async(s_root);  // may run inside the root's own event
  s_root = nullptr;
  s_img[0] = s_img[1] = nullptr;
  s_content = s_clock = s_hh = s_colon = s_mm = s_ampm = s_date = nullptr;
  s_wx = s_wx_icon = s_wx_temp = s_wx_text = s_cards = s_hint = nullptr;
  s_pc = s_media = s_coin = Card{};
  backlight::set(app::screen_brightness());
  lv_disp_trig_activity(nullptr);  // closing counts as activity: the countdown starts again
}

void tick() {
  if (!s_root) return;
  const uint32_t now = lv_tick_get();
  s_colon_on = !s_colon_on;
  lv_obj_set_style_text_opa(s_colon, s_colon_on ? LV_OPA_COVER : LV_OPA_30, 0);
  update_clock();
  if (now - s_coin_ms >= kCoinMs) {
    s_coin_ms = now;
    update_coin(true);
  }
  const storage::Screensaver &ss = app::state().settings.ss;
  if (static_cast<storage::SsBg>(ss.bg_mode) == storage::SsBg::Cycle &&
      now - s_bg_ms >= static_cast<uint32_t>(ss.cycle_min) * 60000u) {
    s_bg_ms = now;
    next_wallpaper();
  }
  if (now - s_drift_ms >= kDriftMs) {
    s_drift_ms = now;
    drift();
  }
}

void on_data() {
  if (s_root) update_data();
}

// ---- wallpaper picker -----------------------------------------------------------------------------
namespace {
constexpr int kMaxTiles = 16;
constexpr int kThumbW = WALLPAPER_W / 4;  // 80: the 1/4-size decode, shown 1:1
constexpr int kThumbH = WALLPAPER_H / 4;  // 120
lv_obj_t *s_sheet = nullptr;
lv_obj_t *s_tiles[kMaxTiles] = {};
wallpaper::Image *s_thumbs = nullptr;

void close_picker() {
  if (!s_sheet) return;
  lv_obj_del_async(s_sheet);
  s_sheet = nullptr;
  if (s_thumbs) {
    for (int i = 0; i < wallpaper::count() && i < kMaxTiles; i++) wallpaper::release(s_thumbs[i]);
    delete[] s_thumbs;
    s_thumbs = nullptr;
  }
}

void mark_selected() {
  const int sel = app::state().settings.ss.bg;
  for (int i = 0; i < wallpaper::count() && i < kMaxTiles; i++) {
    if (!s_tiles[i]) continue;
    lv_obj_t *frame = lv_obj_get_child(s_tiles[i], 0);
    lv_obj_t *check = lv_obj_get_child(frame, 1);
    lv_obj_set_style_border_color(frame, c(i == sel ? kCpu : kBorder), 0);
    lv_obj_set_style_border_width(frame, i == sel ? 3 : 1, 0);
    show(check, i == sel);
    lv_obj_set_style_text_opa(lv_obj_get_child(frame, 2), i == sel ? LV_OPA_COVER : LV_OPA_80, 0);
  }
}

void tile_cb(lv_event_t *e) {
  storage::Screensaver ss = app::state().settings.ss;
  ss.bg = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  s_cycle_next = ss.bg;  // a rotation restarts from the chosen image
  app::set_screensaver(ss);
  mark_selected();
  settings::on_screensaver();
}
}  // namespace

void open_picker() {
  close_picker();
  const int n = wallpaper::count() < kMaxTiles ? wallpaper::count() : kMaxTiles;
  s_thumbs = new (std::nothrow) wallpaper::Image[n];
  s_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_sheet);
  lv_obj_set_size(s_sheet, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_sheet, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_top(s_sheet, 8, 0);
  lv_obj_set_style_pad_hor(s_sheet, kMargin, 0);
  lv_obj_set_style_pad_row(s_sheet, 8, 0);
  lv_obj_set_flex_flow(s_sheet, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE);

  // Header: [x] title / hint (two lines: the portrait screen is narrow). A tap on an image applies it
  // at once, so the close button is all the sheet needs.
  lv_obj_t *head = lv_obj_create(s_sheet);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), 40);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 10, 0);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *x = lv_btn_create(head);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, 36, 36);
  lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(x, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(x, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(x, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(x, 8);
  lv_obj_center(make_label(x, font_body(), kText, LV_SYMBOL_CLOSE));
  lv_obj_add_event_cb(x, [](lv_event_t *) { close_picker(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *titles = box(head);
  lv_obj_set_height(titles, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(titles, 1);
  lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);
  lv_obj_t *title = make_label(titles, font_body(), kText, tr(Str::SsPickerTitle));
  lv_obj_set_width(title, LV_PCT(100));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_t *hint = make_label(titles, font_tiny(), kMuted, tr(Str::SsPickerHint));
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);

  // Grid: 3 per row; each tile = rounded thumbnail (1/4-size decode, 80 x 120 shown 1:1) with its
  // name in the calm, dark top band of the image (where the clock goes), up to two lines: some names
  // are wider than a 96 px column, and under the image they would push the last row off the screen.
  // All eight fit without scrolling.
  lv_obj_t *grid = lv_obj_create(s_sheet);
  lv_obj_remove_style_all(grid);
  lv_obj_set_width(grid, LV_PCT(100));
  lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_top(grid, 4, 0);
  lv_obj_set_style_pad_row(grid, 12, 0);
  lv_obj_set_style_pad_column(grid, 4, 0);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);
  const int tw = (kScreenW - 2 * kMargin - 2 * 4) / 3;  // 96 per column, thumbnail centred
  for (int i = 0; i < n; i++) {
    lv_obj_t *tile = box(grid);
    lv_obj_set_size(tile, tw, kThumbH);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_t *frame = box(tile);
    lv_obj_set_size(frame, kThumbW, kThumbH);
    lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(frame, 10, 0);
    lv_obj_set_style_clip_corner(frame, true, 0);
    lv_obj_set_style_bg_color(frame, c(kSurface), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_post(frame, true, 0);  // the selection ring is drawn over the image
    lv_obj_t *img = lv_img_create(frame);
    if (s_thumbs && wallpaper::decode(i, s_thumbs[i], true)) lv_img_set_src(img, &s_thumbs[i].dsc);
    lv_obj_center(img);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *check = box(frame);
    lv_obj_set_size(check, 24, 24);
    lv_obj_align(check, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_set_style_radius(check, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(check, c(kCpu), 0);
    lv_obj_set_style_bg_opa(check, LV_OPA_COVER, 0);
    lv_obj_center(make_label(check, font_small(), kBg, LV_SYMBOL_OK));
    lv_obj_t *name = make_label(frame, font_tiny(), kText, wallpaper::name(i));
    lv_obj_set_width(name, kThumbW - 8);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 7);
    s_tiles[i] = tile;
  }
  for (int i = n; i < kMaxTiles; i++) s_tiles[i] = nullptr;
  mark_selected();
}

void on_rebuild() {
  stop();
  close_picker();
}

}  // namespace screensaver
}  // namespace ui
