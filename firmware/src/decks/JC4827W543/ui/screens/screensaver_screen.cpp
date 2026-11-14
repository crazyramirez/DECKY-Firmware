// Screensaver: a full-screen wallpaper (fixed, random at every start, or rotating with a slow
// crossfade), a big clock with a blinking colon, the long date, the weather line and glass cards with
// the data the user picked in Settings (PC, now playing, crypto). It lives on the top layer above
// every page; a touch (swallowed: it never presses what is underneath) or the BOOT button closes it.
// Tapping a card opens what it shows (PC -> Monitor, music -> Player, crypto -> that coin's chart,
// weather line -> Today).
// While it is enabled it replaces auto deep sleep (app_controller): the deck never sleeps by itself.
// Anti burn-in: the content drifts a few pixels every minute and the colon blinks.
//
// Also the wallpaper picker sheet opened from Settings › Screensaver › Image.
//
// 480 x 272 layout (content box 448 x 248 before drift):
//   clock 84 px (digits font) · date 20 px · weather line (28 px icon + temperature)
//   ... calm band of the wallpaper ...
//   three 144 x 64 glass cards: PC (CPU / GPU table) · now playing (cover) · crypto (logo)

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
#include "ui/battery_badge.h"
#include "ui/ui_internal.h"
#include "ui/wallpaper.h"

namespace ui {
namespace screensaver {
namespace {

using namespace theme;

// Layout (480 x 272).
constexpr int kEdgeX = 16;         // content inset from the screen edge (before drift)
constexpr int kEdgeY = 12;
constexpr int kDrift = 6;          // max drift each way, px
constexpr int kContentW = kScreenW - 2 * kEdgeX;  // 448
constexpr int kContentH = kScreenH - 2 * kEdgeY;  // 248
constexpr int kCardGap = 8;
constexpr int kCardW = (kContentW - 2 * kCardGap) / 3;  // 144: three cards fill the row
constexpr int kCardH = 64;
constexpr int kCardPad = 8;
constexpr int kCardThumb = 40;     // cover / logo box inside a card
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

// PC card: a two-row table (name · temperature · load) instead of an icon box, so the values fit.
struct PcCard {
  lv_obj_t *root = nullptr;
  lv_obj_t *temp[2] = {};
  lv_obj_t *load[2] = {};
};

lv_obj_t *s_root = nullptr;
BatteryBadge s_bat;  // deck battery, top right
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
PcCard s_pc;
Card s_media, s_coin;
const void *s_cover_src = nullptr;
const void *s_cover_data = nullptr;
uint32_t s_bg_ms = 0, s_coin_ms = 0, s_drift_ms = 0;
int s_coin_pos = 0;
int s_coin_cat = -1;  // catalog index of the coin on the crypto card (tap target)
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

lv_obj_t *row(lv_obj_t *parent, int gap) {
  lv_obj_t *r = box(parent);
  lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, gap, 0);
  return r;
}

void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (!on && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Frosted-glass card: dark translucent fill, hairline light border.
lv_obj_t *glass(lv_obj_t *parent) {
  lv_obj_t *o = box(parent);
  lv_obj_set_size(o, kCardW, kCardH);
  lv_obj_set_style_radius(o, 14, 0);
  lv_obj_set_style_bg_color(o, c(kGlass), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_60, 0);
  lv_obj_set_style_border_color(o, lv_color_white(), 0);
  lv_obj_set_style_border_opa(o, LV_OPA_20, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_pad_all(o, kCardPad, 0);
  return o;
}

Card make_card(lv_obj_t *parent) {
  Card k;
  k.root = glass(parent);
  k.box = box(k.root);
  lv_obj_set_size(k.box, kCardThumb, kCardThumb);
  lv_obj_align(k.box, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(k.box, 10, 0);
  lv_obj_set_style_clip_corner(k.box, true, 0);
  lv_obj_set_style_bg_color(k.box, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(k.box, LV_OPA_10, 0);
  k.icon = make_label(k.box, font_title(), kText, "");
  lv_obj_center(k.icon);
  k.img = lv_img_create(k.box);
  lv_obj_center(k.img);
  lv_obj_add_flag(k.img, LV_OBJ_FLAG_HIDDEN);
  const int tx = kCardThumb + 8, tw = kCardW - 2 * kCardPad - tx;  // 80 px of text
  k.line1 = make_label(k.root, font_small(), kText, "");
  lv_obj_set_size(k.line1, tw, lv_font_get_line_height(font_small()));
  lv_label_set_long_mode(k.line1, LV_LABEL_LONG_DOT);
  lv_obj_align(k.line1, LV_ALIGN_LEFT_MID, tx, -9);
  k.line2 = make_label(k.root, font_tiny(), kMuted, "");
  lv_obj_set_size(k.line2, tw, lv_font_get_line_height(font_tiny()));
  lv_label_set_long_mode(k.line2, LV_LABEL_LONG_DOT);
  lv_obj_align(k.line2, LV_ALIGN_LEFT_MID, tx, 10);
  return k;
}

PcCard make_pc_card(lv_obj_t *parent) {
  PcCard k;
  k.root = glass(parent);
  lv_obj_set_style_pad_hor(k.root, 12, 0);
  const char *names[2] = {"CPU", "GPU"};
  const uint32_t colors[2] = {kCpu, kGpu};
  for (int i = 0; i < 2; i++) {
    const int y = i == 0 ? -11 : 11;
    lv_obj_t *n = make_label(k.root, font_tiny(), colors[i], names[i]);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, y);
    k.temp[i] = make_label(k.root, font_body(), kText, "");
    lv_obj_set_width(k.temp[i], 46);
    lv_obj_set_style_text_align(k.temp[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(k.temp[i], LV_ALIGN_LEFT_MID, 28, y);
    k.load[i] = make_label(k.root, font_small(), kMuted, "");
    lv_obj_set_width(k.load[i], 44);
    lv_obj_set_style_text_align(k.load[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(k.load[i], LV_ALIGN_RIGHT_MID, 0, y);
  }
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
  lv_obj_set_pos(s_content, kEdgeX + dx, kEdgeY + dy);
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

bool update_pc() {
  const auto &st = app::state();
  const bool on = (st.settings.ss.show & storage::kSsPc) && st.has_snapshot && st.link == app::LinkState::Online;
  show(s_pc.root, on);
  if (!on) return false;
  const float temp[2] = {st.snap.cpu.temp_c, st.snap.gpu.temp_c};
  const float load[2] = {st.snap.cpu.load_pct, st.snap.gpu.load_pct};
  char b[16];
  for (int i = 0; i < 2; i++) {
    if (telemetry::has(temp[i]))
      snprintf(b, sizeof(b), "%.0f°", static_cast<double>(temp[i]));
    else
      snprintf(b, sizeof(b), "--");
    set_text(s_pc.temp[i], b);
    if (telemetry::has(load[i]))
      snprintf(b, sizeof(b), "%.0f %%", static_cast<double>(load[i]));
    else
      b[0] = '\0';
    set_text(s_pc.load[i], b);
  }
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
  // The Player reuses one descriptor for every cover: follow its pixels too, not only the pointer.
  if (cover != s_cover_src || (cover && cover->data != s_cover_data)) {
    s_cover_src = cover;
    s_cover_data = cover ? cover->data : nullptr;
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
  char b[48], p[40];
  if (cat >= 0) {
    lv_img_set_src(s_coin.img, COIN_CATALOG[cat].logo_m);
    lv_obj_center(s_coin.img);
  }
  s_coin_cat = cat;
  show(s_coin.img, cat >= 0);
  show(s_coin.icon, cat < 0);
  // Price on the main line; symbol and 24 h change (coloured) below.
  cfmt::money(cn.price, -1, s_m->cur, p, sizeof(p));
  set_text(s_coin.line1, p);
  const char *sym = cat >= 0 ? COIN_CATALOG[cat].sym : cn.pair;
  if (!isnan(cn.chg_pct)) {
    cfmt::pct(cn.chg_pct, p, sizeof(p));
    snprintf(b, sizeof(b), "%s  %s %s", sym, cn.chg_pct >= 0 ? PCD_SYM_CARET_UP : PCD_SYM_CARET_DOWN, p);
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

void close_picker();  // the wallpaper picker (below), in case the screensaver started over it

// Tapping a card (or the weather line) closes the screensaver and opens what it shows. A finger that
// slides off just closes it, like a tap anywhere else.
enum class Target : uint8_t { Monitor, Player, Coin, Today };

void open_cb(lv_event_t *e) {
  const auto t = static_cast<Target>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const int coin = s_coin_cat;
  stop();
  close_picker();
  switch (t) {
    case Target::Monitor: navigate(Page::Home); break;
    case Target::Player: navigate(Page::Controls); break;
    case Target::Today: navigate(Page::Today); break;
    case Target::Coin:
      if (coin >= 0)
        crypto_detail::show(coin);
      else
        navigate(Page::Crypto);
      break;
  }
}

void tappable(lv_obj_t *o, Target t) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(o, lv_color_white(), LV_STATE_PRESSED);  // subtle pressed state
  lv_obj_set_style_bg_opa(o, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_border_opa(o, LV_OPA_60, LV_STATE_PRESSED);
  lv_obj_add_event_cb(o, open_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(t)));
  lv_obj_add_event_cb(o, close_cb, LV_EVENT_PRESS_LOST, nullptr);
}

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
  lv_obj_set_pos(s_content, kEdgeX, kEdgeY);

  // Clock: HH : MM (colon blinks) + AM/PM, centred in the calm upper band of the wallpapers.
  const int clock_h = lv_font_get_line_height(font_clock_xl());  // 61
  constexpr int kClockY = 4;
  s_clock = row(s_content, 4);
  lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, kClockY);
  s_hh = make_label(s_clock, font_clock_xl(), kText, "--");
  s_colon = make_label(s_clock, font_clock_xl(), kText, ":");
  lv_obj_set_style_translate_y(s_colon, -5, 0);
  s_mm = make_label(s_clock, font_clock_xl(), kText, "--");
  s_ampm = make_label(s_clock, font_title(), kText, "");
  lv_obj_set_style_pad_bottom(s_ampm, 6, 0);
  lv_obj_set_style_pad_left(s_ampm, 4, 0);
  const int date_y = kClockY + clock_h + 4;
  s_date = make_label(s_content, font_title(), kText, "");
  lv_obj_set_style_text_opa(s_date, LV_OPA_90, 0);
  lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, date_y);
  // Weather line: icon, temperature, condition · place.
  // (a pill: transparent, lit while pressed; its padding is taken back from the gap above)
  s_wx = row(s_content, 10);
  lv_obj_set_flex_align(s_wx, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(s_wx, 10, 0);
  lv_obj_set_style_pad_ver(s_wx, 2, 0);
  lv_obj_set_style_radius(s_wx, 14, 0);
  lv_obj_align(s_wx, LV_ALIGN_TOP_MID, 0, date_y + lv_font_get_line_height(font_title()) + 2);
  tappable(s_wx, Target::Today);
  s_wx_icon = make_label(s_wx, font_value(), kText, "");
  s_wx_temp = make_label(s_wx, font_value(), kText, "");
  s_wx_text = make_label(s_wx, font_small(), kText, "");
  lv_obj_set_style_text_opa(s_wx_text, LV_OPA_80, 0);
  lv_obj_set_style_max_width(s_wx_text, kContentW - 130, 0);  // long place names end in "..."
  lv_label_set_long_mode(s_wx_text, LV_LABEL_LONG_DOT);

  // Cards along the bottom (only the ones with something to show; the row stays centred).
  s_cards = row(s_content, kCardGap);
  lv_obj_set_flex_align(s_cards, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(s_cards, LV_ALIGN_BOTTOM_MID, 0, 0);
  s_pc = make_pc_card(s_cards);
  tappable(s_pc.root, Target::Monitor);
  s_media = make_card(s_cards);
  tappable(s_media.root, Target::Player);
  set_color(s_media.icon, kGpu);
  lv_label_set_long_mode(s_media.line1, LV_LABEL_LONG_SCROLL_CIRCULAR);  // long titles glide by
  s_coin = make_card(s_cards);
  tappable(s_coin.root, Target::Coin);
  set_text(s_coin.icon, PCD_SYM_BITCOIN);
  set_color(s_coin.icon, kCrypto);
  lv_obj_set_style_bg_opa(s_coin.box, LV_OPA_TRANSP, 0);  // round logos sit on the glass directly
  s_cover_src = s_cover_data = nullptr;

  s_hint = make_label(s_root, font_tiny(), kText, tr(Str::SsTapToClose));
  lv_obj_set_style_text_opa(s_hint, LV_OPA_70, 0);
  lv_obj_align(s_hint, LV_ALIGN_TOP_RIGHT, -10, 6);
  // Deck battery, top right (only on battery, as Settings > Screensaver > Battery says).
  battery_badge_create(s_bat, s_root, font_tiny(), 20, 10);
  lv_obj_align(s_bat.root, LV_ALIGN_TOP_RIGHT, -10, 6);
  if (!lv_obj_has_flag(s_bat.root, LV_OBJ_FLAG_HIDDEN)) {  // the hint goes to its left
    lv_obj_update_layout(s_root);
    lv_obj_align_to(s_hint, s_bat.root, LV_ALIGN_OUT_LEFT_MID, -15, 0);
  }
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
  s_pc = PcCard{};
  s_media = s_coin = Card{};
  s_bat = BatteryBadge{};
  backlight::set(app::screen_brightness());
  lv_disp_trig_activity(nullptr);  // closing counts as activity: the countdown starts again
}

void tick() {
  if (!s_root) return;
  battery_badge_update(s_bat);
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
lv_obj_t *s_sheet = nullptr;
lv_obj_t *s_tiles[kMaxTiles] = {};
wallpaper::Image *s_thumbs = nullptr;
int s_thumb_n = 0;

void close_picker() {
  if (!s_sheet) return;
  lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);  // never drawn again: its thumbnails are freed now
  lv_obj_del_async(s_sheet);
  s_sheet = nullptr;
  if (s_thumbs) {
    for (int i = 0; i < s_thumb_n; i++) wallpaper::release(s_thumbs[i]);
    delete[] s_thumbs;
    s_thumbs = nullptr;
  }
  s_thumb_n = 0;
}

void mark_selected() {
  const int sel = app::state().settings.ss.bg;
  for (int i = 0; i < kMaxTiles; i++) {
    if (!s_tiles[i]) continue;
    lv_obj_t *frame = lv_obj_get_child(s_tiles[i], 0);
    lv_obj_t *check = lv_obj_get_child(frame, 1);
    lv_obj_set_style_border_color(frame, c(i == sel ? kCpu : kBorder), 0);
    lv_obj_set_style_border_width(frame, i == sel ? 3 : 1, 0);
    show(check, i == sel);
    set_color(lv_obj_get_child(s_tiles[i], 1), i == sel ? kText : kMuted);
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
  s_thumb_n = s_thumbs ? n : 0;
  s_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_sheet);
  lv_obj_set_size(s_sheet, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_sheet, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_sheet, kMargin, 0);
  lv_obj_set_style_pad_row(s_sheet, 8, 0);
  lv_obj_set_flex_flow(s_sheet, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE);

  // Header: [x] title / hint below ................ [Done]
  lv_obj_t *head = lv_obj_create(s_sheet);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), 42);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 8, 0);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *x = lv_btn_create(head);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, 32, 32);
  lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(x, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(x, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(x, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(x, 8);
  lv_obj_center(make_label(x, font_small(), kText, LV_SYMBOL_CLOSE));
  lv_obj_add_event_cb(x, [](lv_event_t *) { close_picker(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *titles = box(head);
  lv_obj_set_height(titles, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(titles, 1);
  lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_left(titles, 2, 0);
  lv_obj_t *title = make_label(titles, font_title(), kText, tr(Str::SsPickerTitle));
  lv_obj_set_width(title, LV_PCT(100));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_t *hint = make_label(titles, font_tiny(), kMuted, tr(Str::SsPickerHint));
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
  lv_obj_t *ok = lv_btn_create(head);
  lv_obj_remove_style_all(ok);
  lv_obj_set_size(ok, 84, 32);
  lv_obj_set_style_radius(ok, 16, 0);
  lv_obj_set_style_bg_color(ok, c(kCpu), 0);
  lv_obj_set_style_bg_color(ok, c(0x3FB3C4), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, 0);
  lv_obj_set_ext_click_area(ok, 6);
  lv_obj_center(make_label(ok, font_small(), kBg, tr(Str::CryDone)));
  lv_obj_add_event_cb(ok, [](lv_event_t *) { close_picker(); }, LV_EVENT_CLICKED, nullptr);

  // Grid: 4 per row; each tile = rounded thumbnail (1/4-size decode, 120 x 68, centre-cropped to
  // the frame) + name.
  constexpr int kCols = 4, kColGap = 8;
  lv_obj_t *grid = lv_obj_create(s_sheet);
  lv_obj_remove_style_all(grid);
  lv_obj_set_width(grid, LV_PCT(100));
  lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 10, 0);
  lv_obj_set_style_pad_column(grid, kColGap, 0);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  const int tw = (kScreenW - 2 * kMargin - (kCols - 1) * kColGap) / kCols;  // 110
  const int th = tw * WALLPAPER_H / WALLPAPER_W;                           // 62
  const int name_h = lv_font_get_line_height(font_tiny());
  for (int i = 0; i < n; i++) {
    lv_obj_t *tile = box(grid);
    lv_obj_set_size(tile, tw, th + 4 + name_h);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_t *frame = box(tile);
    lv_obj_set_size(frame, tw, th);
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
    lv_obj_set_size(check, 22, 22);
    lv_obj_align(check, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_set_style_radius(check, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(check, c(kCpu), 0);
    lv_obj_set_style_bg_opa(check, LV_OPA_COVER, 0);
    lv_obj_center(make_label(check, font_tiny(), kBg, LV_SYMBOL_OK));
    lv_obj_t *name = make_label(tile, font_tiny(), kMuted, wallpaper::name(i));
    lv_obj_set_width(name, tw - 2);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_BOTTOM_LEFT, 2, 0);
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
