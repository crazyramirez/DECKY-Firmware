// First-run setup wizard, laid out for the 320x480 portrait panel: a full-screen sheet on the top
// layer with a header, a 5-dot stepper, one card per step and Back / Next at the bottom.
//
//   1 Language  ->  2 Wi-Fi  ->  3 Pair PC  ->  4 Location (+ °C/°F, 24/12 h)  ->  5 Done
//
// It opens at power-on when no Wi-Fi network is stored, and from Settings > Setup wizard. Every
// step reuses the existing sheets (Wi-Fi password, pairing code, manual IP, city search), which open
// above it on the same layer. Every step can be skipped; nothing here is required to use the deck.
// Repaints are kept to what changed: each one is a full-frame push on this panel.

#include <stdio.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/power.h"
#include "app/state_store.h"
#include "network/net_task.h"
#include "storage/net_store.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace setup {
namespace {

using namespace theme;

enum Step : int { kLang, kWifi, kPair, kPlace, kDone, kSteps };

// Chrome (lives as long as the wizard).
lv_obj_t *s_root = nullptr;
lv_obj_t *s_sub = nullptr;  // "Paso 2 de 5"
lv_obj_t *s_dot[kSteps] = {};
lv_obj_t *s_dot_lbl[kSteps] = {};
lv_obj_t *s_dot_name[kSteps] = {};
lv_obj_t *s_line[kSteps - 1] = {};
lv_obj_t *s_card = nullptr;
lv_obj_t *s_back = nullptr;
lv_obj_t *s_next = nullptr;
lv_obj_t *s_next_lbl = nullptr;
int s_step = kLang;
int s_drawn = -1;          // step + done mask last painted (chrome repaints only on change)
bool s_lang_seen = false;  // the language step counts as done once the user moved past it

// Widgets of the current step (reset on every step change).
lv_obj_t *s_status = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_spinner = nullptr;
lv_obj_t *s_refresh = nullptr;
lv_obj_t *s_need_wifi = nullptr;
lv_obj_t *s_place_btn_lbl = nullptr;
lv_obj_t *s_units = nullptr;
lv_obj_t *s_clock = nullptr;
lv_obj_t *s_done_icon = nullptr;
lv_obj_t *s_done_title = nullptr;
lv_obj_t *s_done_hint = nullptr;
lv_obj_t *s_sum[3] = {};
int s_pair_wifi = -1;  // pair step: Wi-Fi state the body was laid out for (-1 = not yet)
int s_done_all = -1;   // done step: "all set" state last painted

net::ScanEntry s_nets[net::kMaxScan];
// Wi-Fi step: scans when the step is shown and every 15 s while it stays in view.
constexpr uint32_t kRescanMs = 15000;
constexpr uint32_t kScanLostMs = 20000;
bool s_scanning = false;
uint32_t s_scan_ms = 0;
net::PcEntry s_pcs[net::kMaxPcs];

const char *s_units_map[] = {"°C", "°F", ""};
const char *s_clock_map[] = {"24 h", "12 h", ""};

void build_step();
void refresh();

uint8_t done_mask(const net::Status &ns) {
  const auto &st = app::state();
  uint8_t m = 0;
  if (s_lang_seen) m |= 1u << kLang;
  if (ns.wifi_connected) m |= 1u << kWifi;
  if (ns.paired && st.link != app::LinkState::Unauthorized) m |= 1u << kPair;
  if (st.settings.place_name[0]) m |= 1u << kPlace;
  return m;
}

// ---- navigation -------------------------------------------------------------------------------------
void go_now(int step) {
  if (!s_root || step < 0 || step >= kSteps || step == s_step) return;
  if (s_step == kLang) s_lang_seen = true;
  s_step = step;
  build_step();
}

// Step changes clean the card, and the tap that asked for it may come from a child of that card:
// run them after the event.
void go(int step) {
  lv_async_call([](void *p) { go_now(static_cast<int>(reinterpret_cast<intptr_t>(p))); },
                reinterpret_cast<void *>(static_cast<intptr_t>(step)));
}

void close_now() {
  if (!s_root) return;
  lv_obj_del_async(s_root);  // we may be inside an event of one of its children
  s_root = s_sub = s_card = s_back = s_next = s_next_lbl = nullptr;
  for (auto &o : s_dot) o = nullptr;
  for (auto &o : s_dot_lbl) o = nullptr;
  for (auto &o : s_dot_name) o = nullptr;
  for (auto &o : s_line) o = nullptr;
  s_status = s_list = s_spinner = s_refresh = s_need_wifi = s_place_btn_lbl = nullptr;
  s_units = s_clock = s_done_icon = s_done_title = s_done_hint = nullptr;
  for (auto &o : s_sum) o = nullptr;
}

// ---- small building blocks ------------------------------------------------------------------------------
lv_obj_t *box(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *round_button(lv_obj_t *parent, const char *sym, int size, lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, size, size);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(b, 8);
  lv_obj_t *l = make_label(b, font_body(), kText, sym);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

lv_obj_t *spinner(lv_obj_t *parent) {
  lv_obj_t *s = lv_spinner_create(parent, 1000, 60);
  lv_obj_set_size(s, 24, 24);
  lv_obj_set_style_arc_width(s, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s, c(kSurfaceHi), LV_PART_MAIN);
  lv_obj_set_style_arc_color(s, c(kCpu), LV_PART_INDICATOR);
  lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  return s;
}

void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (on)
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *wrap_label(lv_obj_t *parent, const char *text, uint32_t color) {
  lv_obj_t *l = make_label(parent, font_small(), color, text);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  return l;
}

// Step header inside the card: tinted icon disc + title. Returns the row (for a button on the right).
lv_obj_t *step_head(const char *sym, uint32_t color, const char *title) {
  lv_obj_t *h = box(s_card, LV_PCT(100), 40);
  lv_obj_t *disc = box(h, 40, 40);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(disc, lv_color_mix(c(color), c(kSurface), 60), 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_align(disc, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *i = make_label(disc, font_title(), color, sym);
  lv_obj_center(i);
  lv_obj_t *t = make_label(h, font_title(), kText, title);
  lv_obj_set_width(t, 220);  // titles next to a button (Wi-Fi, Pair PC) are short
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 52, 0);
  return h;
}

// Scrolling list well inside the card (networks, PCs).
lv_obj_t *list(lv_obj_t *parent) {
  lv_obj_t *l = lv_obj_create(parent);
  lv_obj_remove_style_all(l);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_flex_grow(l, 1);
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_radius(l, 10, 0);
  lv_obj_set_style_bg_color(l, c(kBg), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(l, 8, 0);
  lv_obj_set_style_pad_ver(l, 2, 0);
  lv_obj_set_scroll_dir(l, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_ACTIVE);
  return l;
}

lv_obj_t *list_row(lv_obj_t *l, lv_event_cb_t cb, intptr_t data) {
  lv_obj_t *r = box(l, LV_PCT(100), kTouchMin);
  lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(r, c(kBorder), 0);
  lv_obj_set_style_border_opa(r, LV_OPA_60, 0);
  lv_obj_set_style_border_width(r, 1, 0);
  lv_obj_set_style_pad_hor(r, 4, 0);
  lv_obj_set_style_bg_color(r, c(kSurfaceHi), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(data));
  return r;
}

void list_message(const char *text, uint32_t color) {
  lv_obj_t *m = wrap_label(s_list, text, color);
  lv_obj_set_style_pad_ver(m, 10, 0);
  lv_obj_set_style_pad_hor(m, 4, 0);
}

lv_obj_t *accent_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **label_out = nullptr) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, LV_PCT(100), 46);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_bg_color(b, c(kCpu), 0);
  lv_obj_set_style_bg_color(b, c(0x3FB3C4), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_t *l = make_label(b, font_body(), kBg, text);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  if (label_out) *label_out = l;
  return b;
}

// Segmented control, same look as Settings.
lv_obj_t *segmented(lv_obj_t *parent, const char **map, uint16_t sel, lv_event_cb_t cb) {
  lv_obj_t *m = lv_btnmatrix_create(parent);
  lv_btnmatrix_set_map(m, map);
  lv_btnmatrix_set_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(m, true);
  lv_btnmatrix_set_btn_ctrl(m, sel, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_height(m, 40);
  lv_obj_set_flex_grow(m, 1);
  lv_obj_set_style_pad_all(m, 3, 0);
  lv_obj_set_style_pad_gap(m, 3, 0);
  lv_obj_set_style_bg_color(m, c(kBg), 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_radius(m, 10, 0);
  lv_obj_set_style_bg_color(m, c(kSurface), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, c(kBorder), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(m, c(kText), LV_PART_ITEMS);
  lv_obj_set_style_text_font(m, font_body(), LV_PART_ITEMS);
  lv_obj_set_style_radius(m, 8, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(m, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return m;
}

void sync_segmented(lv_obj_t *m, uint16_t sel) {
  if (m && !lv_btnmatrix_has_btn_ctrl(m, sel, LV_BTNMATRIX_CTRL_CHECKED))
    lv_btnmatrix_set_btn_ctrl(m, sel, LV_BTNMATRIX_CTRL_CHECKED);
}

// ---- 1 · language -----------------------------------------------------------------------------------------
void lang_cb(lv_event_t *e) {
  const auto l = static_cast<storage::Lang>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  app::set_language(l);  // no-op if unchanged; otherwise the UI (and this wizard) is rebuilt after the event
}

void lang_option(const char *name, storage::Lang lang) {
  const bool sel = app::state().settings.lang == lang;
  lv_obj_t *r = box(s_card, LV_PCT(100), 54);
  lv_obj_set_style_radius(r, 12, 0);
  lv_obj_set_style_bg_color(r, c(sel ? kSurfaceHi : kBg), 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(r, c(kSurfaceHi), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(r, c(sel ? kCpu : kBorder), 0);
  lv_obj_set_style_border_width(r, sel ? 2 : 1, 0);
  lv_obj_set_style_pad_hor(r, 16, 0);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(r, lang_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(lang)));
  lv_obj_t *n = make_label(r, font_body(), kText, name);
  lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
  if (sel) {
    lv_obj_t *k = make_label(r, font_body(), kCpu, LV_SYMBOL_OK);
    lv_obj_align(k, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

void build_lang() {
  step_head(LV_SYMBOL_KEYBOARD, kPower, "Idioma · Language");
  wrap_label(s_card, tr(Str::WizLangDesc), kMuted);
  lang_option("Español", storage::Lang::Es);
  lang_option("English", storage::Lang::En);
}

// ---- 2 · Wi-Fi -----------------------------------------------------------------------------------------
// The list stays while a scan runs (the spinner says it is searching); results replace it.
void start_scan() {
  net::request_scan();
  s_scanning = true;
  s_scan_ms = lv_tick_get();
  show(s_refresh, false);
  show(s_spinner, true);
}

void network_cb(lv_event_t *e) {
  const intptr_t i = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  if (i < 0)
    wifi::open_sheet(nullptr, true);  // hidden network: SSID + password
  else if (i < net::kMaxScan)
    wifi::open_sheet(s_nets[i].ssid, s_nets[i].secure);
}

void add_network(int i) {
  lv_obj_t *r = list_row(s_list, network_cb, i);
  if (i < 0) {
    lv_obj_t *l = make_label(r, font_body(), kCpu, tr(Str::OtherNetwork));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return;
  }
  const net::ScanEntry &n = s_nets[i];
  const uint32_t col = n.rssi >= -60 ? kOk : n.rssi >= -72 ? kPower : kCritical;
  lv_obj_t *ic = make_label(r, font_body(), col, LV_SYMBOL_WIFI);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *nm = make_label(r, font_body(), kText, n.ssid);
  lv_obj_set_size(nm, 320, lv_font_get_line_height(font_body()));  // one line: long SSIDs end in "…"
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_align(nm, LV_ALIGN_LEFT_MID, 28, 0);
  if (n.secure) {
    lv_obj_t *lk = make_label(r, font_small(), kMuted, PCD_SYM_LOCK);
    lv_obj_align(lk, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

// Rebuild the list from the network task's last scan, keeping the scroll position. scanning: a scan
// is running, so "no results yet" reads as "Scanning…".
void fill_nets(bool scanning) {
  const lv_coord_t y = lv_obj_get_scroll_y(s_list);
  lv_obj_clean(s_list);
  const int n = net::scan_results(s_nets, net::kMaxScan);
  if (n <= 0 && scanning)
    list_message(tr(Str::Scanning), kMuted);
  else if (n < 0)
    list_message(tr(Str::ScanFailed), kCritical);
  else if (n == 0)
    list_message(tr(Str::NoNetworks), kMuted);
  for (int i = 0; i < n; i++) add_network(i);
  add_network(-1);
  if (y > 0) {
    lv_obj_update_layout(s_list);
    lv_obj_scroll_to_y(s_list, y, LV_ANIM_OFF);
  }
}

void build_wifi() {
  lv_obj_t *h = step_head(LV_SYMBOL_WIFI, kCpu, tr(Str::WifiTitle));
  s_refresh = round_button(h, LV_SYMBOL_REFRESH, 40, [](lv_event_t *) { start_scan(); });
  lv_obj_align(s_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
  s_spinner = spinner(h);
  lv_obj_align(s_spinner, LV_ALIGN_RIGHT_MID, -8, 0);
  s_status = wrap_label(s_card, "", kMuted);
  s_list = list(s_card);
  fill_nets(true);  // the last list at once, then a fresh scan
  start_scan();
}

// Every second: another scan 15 s after the last results while the step is in view.
void tick_wifi() {
  if (s_step != kWifi || !s_list || wifi::sheet_open() || power::is_screen_off()) return;
  const uint32_t now = lv_tick_get();
  if (s_scanning && now - s_scan_ms < kScanLostMs) return;
  if (s_scanning || now - s_scan_ms >= kRescanMs) start_scan();
}

void refresh_wifi(const net::Status &ns) {
  if (!s_status) return;
  storage::NetConfig cfg;
  storage::load_net(cfg);
  char buf[160];
  uint32_t col = kMuted;
  if (!cfg.has_wifi()) {
    snprintf(buf, sizeof(buf), "%s", tr(Str::WizWifiPick));
  } else if (ns.wifi_connected) {
    // IP on its own line: LVGL also wraps at '.', which would split the address.
    snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s %s\nIP %s", tr(Str::ConnectedTo), cfg.ssid, ns.ip);
    col = kOk;
  } else {
    const net::Failure f = net::classify(ns.last_disconnect_reason);
    const char *why = f == net::Failure::WrongPassword ? tr(Str::WrongPassword)
                      : f == net::Failure::NotFound    ? tr(Str::NetNotFound)
                      : f == net::Failure::Other       ? tr(Str::ConnFailed)
                                                       : "";
    snprintf(buf, sizeof(buf), "%s %s…%s%s", tr(Str::ConnectingTo), cfg.ssid, why[0] ? "\n" : "", why);
    col = why[0] ? kWarning : kMuted;
  }
  memset(&cfg, 0, sizeof(cfg));  // the password must not linger on the stack
  set_text(s_status, buf);
  set_color(s_status, col);
}

// ---- 3 · pair PC ---------------------------------------------------------------------------------------
void start_discovery() {
  net::request_discovery();
  show(s_refresh, false);
  show(s_spinner, true);
  lv_obj_clean(s_list);
  list_message(tr(Str::PairSearching), kMuted);
}

void pc_cb(lv_event_t *e) {
  const intptr_t i = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  if (i < 0)
    pair::open_manual_sheet();
  else if (i < net::kMaxPcs)
    pair::open_code_sheet(s_pcs[i]);
}

void add_pc(int i) {
  lv_obj_t *r = list_row(s_list, pc_cb, i);
  if (i < 0) {
    lv_obj_t *l = make_label(r, font_body(), kCpu, tr(Str::PairManual));
    lv_obj_set_width(l, 380);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *ch = make_label(r, font_small(), kMuted, LV_SYMBOL_RIGHT);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, 0, 0);
    return;
  }
  const net::PcEntry &pc = s_pcs[i];
  lv_obj_t *ic = make_label(r, font_body(), kGpu, LV_SYMBOL_SHUFFLE);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *nm = make_label(r, font_body(), kText, pc.name[0] ? pc.name : pc.host);
  lv_obj_set_width(nm, 240);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_align(nm, LV_ALIGN_LEFT_MID, 28, 0);
  lv_obj_t *ip = make_label(r, font_small(), kMuted, pc.host);
  lv_obj_align(ip, LV_ALIGN_RIGHT_MID, 0, 0);
}

void build_pair() {
  lv_obj_t *h = step_head(LV_SYMBOL_SHUFFLE, kGpu, tr(Str::PairTitle));
  s_refresh = round_button(h, LV_SYMBOL_REFRESH, 40, [](lv_event_t *) { start_discovery(); });
  lv_obj_align(s_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
  s_spinner = spinner(h);
  lv_obj_align(s_spinner, LV_ALIGN_RIGHT_MID, -8, 0);
  s_status = wrap_label(s_card, "", kMuted);
  s_list = list(s_card);
  s_need_wifi = make_button(s_card, "");
  lv_label_set_text_fmt(lv_obj_get_child(s_need_wifi, 0), "%s  %s", LV_SYMBOL_WIFI, tr(Str::PairWifiButton));
  lv_obj_set_width(s_need_wifi, LV_PCT(100));
  lv_obj_set_style_border_color(s_need_wifi, c(kWarning), 0);
  lv_obj_add_event_cb(s_need_wifi, [](lv_event_t *) { go(kWifi); }, LV_EVENT_CLICKED, nullptr);
  show(s_list, false);
  show(s_refresh, false);
  show(s_need_wifi, false);
  s_pair_wifi = -1;  // refresh_pair() lays the body out and starts the search
}

void refresh_pair(const net::Status &ns) {
  if (!s_status) return;
  const int wifi = ns.wifi_connected ? 1 : 0;
  if (wifi != s_pair_wifi) {  // Wi-Fi came up (or went down) while on this step
    s_pair_wifi = wifi;
    show(s_list, wifi);
    show(s_need_wifi, !wifi);
    if (wifi) {
      start_discovery();
    } else {
      show(s_refresh, false);
      show(s_spinner, false);
    }
  }
  char buf[96];
  uint32_t col = kMuted;
  if (!wifi) {
    snprintf(buf, sizeof(buf), "%s", tr(Str::PairNoWifi));
    col = kWarning;
  } else if (ns.paired && ns.agent_name[0] && app::state().link != app::LinkState::Unauthorized) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s %s", tr(Str::PairPairedWith), ns.agent_name);
    col = kOk;
  } else {
    snprintf(buf, sizeof(buf), "%s", tr(Str::WizPairDesc));
  }
  set_text(s_status, buf);
  set_color(s_status, col);
}

// ---- 4 · location ----------------------------------------------------------------------------------------
void build_place() {
  step_head(PCD_SYM_PIN, kCpu, tr(Str::Location));
  wrap_label(s_card, tr(Str::WizPlaceDesc), kMuted);
  s_status = wrap_label(s_card, "", kMuted);
  accent_button(s_card, "", [](lv_event_t *) { place::open(); }, &s_place_btn_lbl);
  lv_obj_t *r = box(s_card, LV_PCT(100), 40);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(r, 10, 0);
  const auto &cfg = app::state().settings;
  s_units = segmented(r, s_units_map, cfg.temp_f ? 1 : 0, [](lv_event_t *e) {
    app::set_temp_f(lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1);
  });
  s_clock = segmented(r, s_clock_map, cfg.clock_12h ? 1 : 0, [](lv_event_t *e) {
    app::set_clock_12h(lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1);
  });
}

void refresh_place() {
  if (!s_status) return;
  const auto &st = app::state();
  const bool has = st.settings.place_name[0] != '\0';
  char buf[64];
  uint32_t col = kMuted;
  if (has) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s", st.settings.place_name);
    col = kOk;
  } else if (!app::data_online()) {
    snprintf(buf, sizeof(buf), "%s", tr(Str::PlaceOffline));  // the search needs Wi-Fi
    col = kWarning;
  } else {
    snprintf(buf, sizeof(buf), "%s", tr(Str::LocationNone));
  }
  set_text(s_status, buf);
  set_color(s_status, col);
  snprintf(buf, sizeof(buf), "%s  %s", has ? PCD_SYM_PIN : PCD_SYM_SEARCH, tr(has ? Str::Change : Str::ChoosePlace));
  set_text(s_place_btn_lbl, buf);
  sync_segmented(s_units, st.settings.temp_f ? 1 : 0);
  sync_segmented(s_clock, st.settings.clock_12h ? 1 : 0);
}

// ---- 5 · done ------------------------------------------------------------------------------------------
lv_obj_t *summary_row(lv_obj_t *parent, bool divider, const char *sym, uint32_t sym_color, const char *key) {
  // icon · key · value (takes whatever the key leaves, "..." if longer)
  lv_obj_t *r = box(parent, LV_PCT(100), 38);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 10, 0);
  if (divider) {
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(r, c(kBorder), 0);
    lv_obj_set_style_border_opa(r, LV_OPA_60, 0);
    lv_obj_set_style_border_width(r, 1, 0);
  }
  lv_obj_t *i = make_label(r, font_body(), sym_color, sym);
  lv_obj_set_width(i, 22);
  lv_obj_set_style_text_align(i, LV_TEXT_ALIGN_CENTER, 0);
  make_label(r, font_body(), kText, key);
  lv_obj_t *v = make_label(r, font_small(), kMuted, "");
  lv_obj_set_flex_grow(v, 1);
  lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  return v;
}

void build_done() {
  lv_obj_set_flex_align(s_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_card, 8, 0);  // icon + title + 3 rows + 2-line hint: ~250 of 264 px
  lv_obj_t *disc = box(s_card, 48, 48);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  s_done_icon = make_label(disc, font_value(), kOk, "");
  lv_obj_center(s_done_icon);
  s_done_title = make_label(s_card, font_title(), kText, "");
  lv_obj_t *sum = box(s_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(sum, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_radius(sum, 10, 0);
  lv_obj_set_style_bg_color(sum, c(kBg), 0);
  lv_obj_set_style_bg_opa(sum, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(sum, 12, 0);
  s_sum[0] = summary_row(sum, false, LV_SYMBOL_WIFI, kCpu, tr(Str::WifiTitle));
  s_sum[1] = summary_row(sum, true, LV_SYMBOL_SHUFFLE, kGpu, tr(Str::WizStepPc));
  s_sum[2] = summary_row(sum, true, PCD_SYM_PIN, kCpu, tr(Str::Location));
  s_done_hint = wrap_label(s_card, "", kMuted);
  lv_obj_set_style_text_align(s_done_hint, LV_TEXT_ALIGN_CENTER, 0);
  s_done_all = -1;
}

void set_summary(lv_obj_t *v, bool ok, const char *ok_text, const char *missing) {
  char buf[64];
  if (ok)
    snprintf(buf, sizeof(buf), "%s " LV_SYMBOL_OK, ok_text);
  else
    snprintf(buf, sizeof(buf), "%s", missing);
  set_text(v, buf);
  set_color(v, ok ? kOk : kMuted);
}

void refresh_done(const net::Status &ns, uint8_t done) {
  if (!s_sum[0]) return;
  storage::NetConfig cfg;
  storage::load_net(cfg);
  char ssid[sizeof(cfg.ssid)];
  strlcpy(ssid, cfg.ssid, sizeof(ssid));
  memset(&cfg, 0, sizeof(cfg));
  set_summary(s_sum[0], done & (1u << kWifi), ssid, tr(Str::NotConnected));
  set_summary(s_sum[1], done & (1u << kPair), ns.agent_name, tr(Str::PairNotPaired));
  set_summary(s_sum[2], done & (1u << kPlace), app::state().settings.place_name, tr(Str::LocationNone));
  const uint8_t need = (1u << kWifi) | (1u << kPair) | (1u << kPlace);
  const int all = (done & need) == need ? 1 : 0;
  if (all == s_done_all) return;
  s_done_all = all;
  const uint32_t col = all ? kOk : kWarning;
  lv_obj_set_style_bg_color(lv_obj_get_parent(s_done_icon), lv_color_mix(c(col), c(kSurface), 60), 0);
  lv_label_set_text(s_done_icon, all ? LV_SYMBOL_OK : LV_SYMBOL_WARNING);
  set_color(s_done_icon, col);
  lv_label_set_text(s_done_title, tr(all ? Str::WizDoneTitle : Str::WizAlmostTitle));
  lv_label_set_text(s_done_hint, tr(all ? Str::WizDoneHint : Str::WizPartialHint));
}

// ---- chrome: stepper + footer ----------------------------------------------------------------------------
Str step_name(int i) {
  switch (i) {
    case kLang: return Str::WizStepLang;
    case kWifi: return Str::WifiTitle;
    case kPair: return Str::WizStepPc;
    case kPlace: return Str::WizStepPlace;
    default: return Str::WizStepDone;
  }
}

void render_chrome(uint8_t done) {
  const int key = s_step | (done << 4);
  if (key == s_drawn) return;
  s_drawn = key;
  char buf[32];
  snprintf(buf, sizeof(buf), tr(Str::WizStepFmt), s_step + 1, static_cast<int>(kSteps));
  set_text(s_sub, buf);
  for (int i = 0; i < kSteps; i++) {
    const bool cur = i == s_step;
    const bool ok = done & (1u << i);
    lv_obj_t *d = s_dot[i];
    lv_obj_set_style_bg_color(d, c(ok ? kOk : cur ? kCpu : kSurface), 0);
    lv_obj_set_style_border_width(d, ok || cur ? 0 : 1, 0);
    lv_obj_set_style_outline_width(d, cur ? 2 : 0, 0);
    char n[8];
    snprintf(n, sizeof(n), "%d", i + 1);
    set_text(s_dot_lbl[i], ok ? LV_SYMBOL_OK : n);
    set_color(s_dot_lbl[i], ok || cur ? kBg : kMuted);
    set_color(s_dot_name[i], cur ? kText : kMuted);
    if (i < kSteps - 1) lv_obj_set_style_bg_color(s_line[i], c(ok ? kOk : kBorder), 0);
  }
  show(s_back, s_step > kLang);
  const bool last = s_step == kDone;
  const bool accent = last || s_step == kLang || (done & (1u << s_step));
  set_text(s_next_lbl, tr(last ? Str::WizStart : accent ? Str::WizNext : Str::WizSkip));
  lv_obj_set_style_bg_color(s_next, c(accent ? kCpu : kSurfaceHi), 0);
  lv_obj_set_style_bg_color(s_next, c(accent ? 0x3FB3C4 : kBorder), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(s_next, accent ? 0 : 1, 0);
  set_color(s_next_lbl, accent ? kBg : kText);
}

void build_stepper() {
  constexpr int kRowH = 54;
  constexpr int kDot = 30;
  constexpr int kDotY = 5;  // room for the 2 px outline (+3 px pad) of the current step
  lv_obj_t *row = box(s_root, LV_PCT(100), kRowH);
  const int w = kScreenW - 2 * kMargin;
  const int slot = w / kSteps;
  for (int i = 0; i < kSteps - 1; i++) {  // connectors first: under the dots
    const int x0 = slot * i + slot / 2 + kDot / 2 + 4;
    const int x1 = slot * (i + 1) + slot / 2 - kDot / 2 - 4;
    s_line[i] = box(row, x1 - x0, 2);
    lv_obj_set_pos(s_line[i], x0, kDotY + kDot / 2 - 1);
    lv_obj_set_style_radius(s_line[i], 1, 0);
    lv_obj_set_style_bg_opa(s_line[i], LV_OPA_COVER, 0);
  }
  for (int i = 0; i < kSteps; i++) {
    lv_obj_t *s = box(row, slot, kRowH);  // whole slot is the touch target
    lv_obj_set_pos(s, slot * i, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        s, [](lv_event_t *e) { go(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)))); },
        LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    s_dot[i] = box(s, kDot, kDot);
    lv_obj_align(s_dot[i], LV_ALIGN_TOP_MID, 0, kDotY);
    lv_obj_set_style_radius(s_dot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_dot[i], c(kBorder), 0);
    lv_obj_set_style_outline_color(s_dot[i], c(kCpu), 0);
    lv_obj_set_style_outline_pad(s_dot[i], 2, 0);
    lv_obj_clear_flag(s_dot[i], LV_OBJ_FLAG_CLICKABLE);
    s_dot_lbl[i] = make_label(s_dot[i], font_small(), kMuted, "");
    lv_obj_center(s_dot_lbl[i]);
    s_dot_name[i] = make_label(s, font_tiny(), kMuted, tr(step_name(i)));
    lv_obj_set_width(s_dot_name[i], slot);
    lv_obj_set_style_text_align(s_dot_name[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_dot_name[i], LV_LABEL_LONG_DOT);
    lv_obj_align(s_dot_name[i], LV_ALIGN_TOP_MID, 0, kDotY + kDot + 5);
  }
}

void next_cb(lv_event_t *) {
  if (s_step < kDone) {
    go(s_step + 1);
    return;
  }
  close_now();  // "Get started"
  navigate(first_tab_page());
}

void build_chrome() {
  s_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_root, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_root, kMargin, 0);
  lv_obj_set_style_pad_row(s_root, 8, 0);
  lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);  // swallow touches meant for the UI underneath
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

  // Header: title + "Paso n de 5", close on the right.
  lv_obj_t *head = box(s_root, LV_PCT(100), 42);
  lv_obj_t *t = make_label(head, font_title(), kText, tr(Str::WizTitle));
  lv_obj_set_width(t, 360);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, -2);
  s_sub = make_label(head, font_small(), kMuted, "");
  lv_obj_align(s_sub, LV_ALIGN_BOTTOM_LEFT, 0, 2);
  lv_obj_t *x = round_button(head, LV_SYMBOL_CLOSE, 38, [](lv_event_t *) {
    close_now();
    toast(tr(Str::WizExitToast), kCpu);
  });
  lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);

  build_stepper();

  s_card = make_card(s_root);
  lv_obj_set_width(s_card, LV_PCT(100));
  lv_obj_set_flex_grow(s_card, 1);
  lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_card, 12, 0);
  lv_obj_set_style_pad_row(s_card, 10, 0);

  // Footer: Back (hidden on the first step) + Next / Skip / Get started.
  lv_obj_t *foot = box(s_root, LV_PCT(100), 48);
  lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(foot, 10, 0);
  s_back = make_button(foot, "");
  lv_label_set_text_fmt(lv_obj_get_child(s_back, 0), "%s %s", LV_SYMBOL_LEFT, tr(Str::Back));
  lv_obj_set_size(s_back, 108, 48);
  lv_obj_add_event_cb(s_back, [](lv_event_t *) { go(s_step - 1); }, LV_EVENT_CLICKED, nullptr);
  s_next = lv_btn_create(foot);
  lv_obj_remove_style_all(s_next);
  lv_obj_set_height(s_next, 48);
  lv_obj_set_flex_grow(s_next, 1);
  lv_obj_set_style_radius(s_next, 12, 0);
  lv_obj_set_style_bg_opa(s_next, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_next, c(kBorder), 0);
  s_next_lbl = make_label(s_next, font_body(), kBg, "");
  lv_obj_center(s_next_lbl);
  lv_obj_add_event_cb(s_next, next_cb, LV_EVENT_CLICKED, nullptr);
}

void build_step() {
  if (!s_card) return;
  lv_obj_clean(s_card);
  lv_obj_set_flex_align(s_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(s_card, 10, 0);
  s_status = s_list = s_spinner = s_refresh = s_need_wifi = s_place_btn_lbl = nullptr;
  s_units = s_clock = s_done_icon = s_done_title = s_done_hint = nullptr;
  for (auto &o : s_sum) o = nullptr;
  switch (s_step) {
    case kLang: build_lang(); break;
    case kWifi: build_wifi(); break;
    case kPair: build_pair(); break;
    case kPlace: build_place(); break;
    default: build_done(); break;
  }
  s_drawn = -1;
  refresh();
}

void refresh() {
  if (!s_root) return;
  const net::Status ns = net::status();
  const uint8_t done = done_mask(ns);
  switch (s_step) {
    case kWifi: refresh_wifi(ns); break;
    case kPair: refresh_pair(ns); break;
    case kPlace: refresh_place(); break;
    case kDone: refresh_done(ns, done); break;
    default: break;
  }
  render_chrome(done);
}

}  // namespace

void open(int step) {
  close_now();
  s_step = step >= 0 && step < kSteps ? step : kLang;
  s_lang_seen = s_step > kLang;  // opened past it: the language is already chosen
  build_chrome();
  build_step();
}

void close() { close_now(); }

bool active() { return s_root != nullptr; }

void rebuild() {
  if (s_root) open(s_step);  // keeps the step (language change rebuilds the whole UI)
}

void tick() {
  refresh();
  if (s_root) tick_wifi();
}

void on_scan() {
  s_scanning = false;
  s_scan_ms = lv_tick_get();  // the next periodic scan counts from here
  if (!s_root || s_step != kWifi || !s_list) return;
  show(s_spinner, false);
  show(s_refresh, true);
  fill_nets(false);
}

void on_discovery() {
  if (!s_root || s_step != kPair || !s_list) return;
  show(s_spinner, false);
  show(s_refresh, true);
  lv_obj_clean(s_list);
  const int n = net::discovery_results(s_pcs, net::kMaxPcs);
  if (n == net::kDiscoveryNoWifi)
    list_message(tr(Str::PairNoWifi), kWarning);
  else if (n <= 0)
    list_message(tr(Str::PairNoneFound), kMuted);
  for (int i = 0; i < n; i++) add_pc(i);
  add_pc(-1);
}

void on_pair_done(const app::PairOutcome &r) {
  if (!s_root) return;
  if (r.ok) toast(tr(Str::PairOk), kOk);
  refresh();
}

void on_place() { refresh(); }

}  // namespace setup


}  // namespace ui
