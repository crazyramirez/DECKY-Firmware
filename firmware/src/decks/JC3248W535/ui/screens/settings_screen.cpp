// Settings, grouped like a native settings app:
//   CONEXIÓN   Wi-Fi ›, PC vinculado ›, Asistente inicial ›
//   SISTEMA    deck: Apagar pantalla, Reposo profundo, Reposo automático · PC: Bloquear, Reiniciar, Apagar
//              (confirmation; PC shutdown/restart only if the PC app allows it)
//   PANTALLA   brightness, language
//   SALVAPANTALLAS on/off, start after, background (fixed / random / rotate), change period, image
//              (picker sheet), data shown (weather, PC, music, crypto), brightness, preview
//   HORA Y TIEMPO weather place (city search), °C/°F, 24/12 h
//   CRIPTO     coins (picker sheet), currency, price refresh, chart style
//   PESTAÑAS   order and visibility of Monitor / Thermals / Player / Today / Crypto in the bottom bar
//              (stored; first visible opens at boot)
//   OPCIONES   thermal alerts
//   INFORMACIÓN firmware, IP, signal, memory, uptime

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "app/app_controller.h"
#include "app/power.h"
#include "app/state_store.h"
#include "board/battery.h"
#include "commands/command_client.h"
#include "crypto/models.h"
#include "crypto/watchlist.h"
#include "esp_heap_caps.h"
#include "network/net_task.h"
#include "ui/ui.h"
#include "ui/cities_ui.h"
#include "ui/ui_internal.h"
#include "ui/wallpaper.h"

namespace ui {
namespace settings {
namespace {

using namespace theme;
using commands::Action;

lv_obj_t *s_wifi_val = nullptr;
lv_obj_t *s_pc_val = nullptr;
lv_obj_t *s_lock = nullptr;
lv_obj_t *s_restart = nullptr;
lv_obj_t *s_shutdown = nullptr;
lv_obj_t *s_power_hint = nullptr;
lv_obj_t *s_bright = nullptr;
lv_obj_t *s_bright_val = nullptr;
lv_obj_t *s_lang = nullptr;
lv_obj_t *s_alerts = nullptr;
lv_obj_t *s_info[7] = {};
lv_obj_t *s_tab_icon[storage::kTabCount] = {};
lv_obj_t *s_tab_lbl[storage::kTabCount] = {};
lv_obj_t *s_tab_up[storage::kTabCount] = {};
lv_obj_t *s_tab_down[storage::kTabCount] = {};
lv_obj_t *s_tab_eye[storage::kTabCount] = {};
const char *s_lang_map[] = {"Español", "English", ""};
const char *s_refresh_map[] = {"1 s", "2 s", "5 s", ""};
constexpr uint8_t kRefreshValues[] = {1, 2, 5};
lv_obj_t *s_refresh = nullptr;
lv_obj_t *s_place_val = nullptr;
lv_obj_t *s_units = nullptr;
lv_obj_t *s_clock = nullptr;
const char *s_units_map[] = {"°C", "°F", ""};
const char *s_clock_map[] = {"24 h", "12 h", ""};
const char *s_sleep_map[] = {"1 min", "2 min", "5 min", "10 min", "", ""};  // [4] = Never (i18n)
constexpr uint8_t kSleepValues[] = {1, 2, 5, 10, 0};
lv_obj_t *s_coins_val = nullptr;
const char *s_cur_map[] = {"USD", "EUR", "MXN", "ARS", "COP", "BRL", ""};
const char *s_crefresh_map[] = {"15 s", "30 s", "60 s", ""};
constexpr uint8_t kCryptoRefresh[] = {15, 30, 60};
const char *s_style_map[] = {"", "", ""};  // Line / Candles (i18n)
// Auto sleep row: greyed out (plus a note, taller row) while the screensaver replaces it.
constexpr int kWideRowH = 92;  // label on top, full-width segmented control below
constexpr int kSleepNoteRowH = 112;
lv_obj_t *s_sleep_row = nullptr;
lv_obj_t *s_sleep_lbl = nullptr;
lv_obj_t *s_sleep_note = nullptr;
lv_obj_t *s_sleep_m = nullptr;
// Screensaver
const char *s_ss_wait_map[] = {"1 min", "2 min", "5 min", "10 min", "30 min", ""};
constexpr uint8_t kSsWait[] = {1, 2, 5, 10, 30};
const char *s_ss_mode_map[] = {"", "", "", ""};       // Fixed / Random / Rotate (i18n)
const char *s_ss_every_map[] = {"1 min", "5 min", "15 min", "30 min", ""};
constexpr uint8_t kSsEvery[] = {1, 5, 15, 30};
// Weather / PC, Music / Crypto as a 2x2 grid: four chips with a check mark don't fit in one row at 320 px.
const char *s_ss_show_map[] = {"", "", "\n", "", "", ""};  // i18n, "✓ " when on
char s_ss_show_txt[4][32];
const char *s_ss_dim_map[] = {"100 %", "60 %", "30 %", ""};
constexpr uint8_t kSsDim[] = {100, 60, 30};
const char *s_ss_bat_map[] = {"", "", "", ""};  // Off / Icon / Icon and % (i18n)
lv_obj_t *s_ss_bat = nullptr;  // only on decks that sense a battery (battery::sensed())
lv_obj_t *s_ss_switch = nullptr;
lv_obj_t *s_ss_wait = nullptr;
lv_obj_t *s_ss_mode = nullptr;
lv_obj_t *s_ss_every = nullptr;
lv_obj_t *s_ss_img_row = nullptr;
lv_obj_t *s_ss_img_val = nullptr;
lv_obj_t *s_ss_show = nullptr;
lv_obj_t *s_ss_dim = nullptr;
lv_obj_t *s_ss_preview = nullptr;

void style_segmented(lv_obj_t *m);

// ---- building blocks ------------------------------------------------------------------------------
lv_obj_t *group(lv_obj_t *page) {
  lv_obj_t *g = make_card(page);
  lv_obj_set_size(g, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(g, 2, 0);
  lv_obj_set_style_pad_hor(g, 12, 0);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_COLUMN);
  return g;
}

lv_obj_t *section(lv_obj_t *page, const char *title) {
  lv_obj_t *h = make_label(page, font_small(), kMuted, title);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  lv_obj_set_style_pad_left(h, 6, 0);
  lv_obj_set_style_pad_top(h, 6, 0);
  lv_obj_t *g = make_card(page);
  lv_obj_set_size(g, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(g, 2, 0);
  lv_obj_set_style_pad_hor(g, 12, 0);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_COLUMN);
  return g;
}

lv_obj_t *row(lv_obj_t *group, bool divider = true, int h = 48) {
  lv_obj_t *r = lv_obj_create(group);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, LV_PCT(100), h);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  if (divider) {
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(r, c(kBorder), 0);
    lv_obj_set_style_border_opa(r, LV_OPA_60, 0);
    lv_obj_set_style_border_width(r, 1, 0);
  }
  return r;
}

lv_obj_t *icon(lv_obj_t *r, const char *sym, uint32_t color) {
  lv_obj_t *i = make_label(r, font_body(), color, sym);
  lv_obj_set_width(i, 24);
  lv_obj_set_style_text_align(i, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(i, LV_ALIGN_LEFT_MID, 0, 0);
  return i;
}

lv_obj_t *label(lv_obj_t *r, const char *text, uint32_t color = kText) {
  lv_obj_t *l = make_label(r, font_body(), color, text);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 34, 0);
  return l;
}

void make_pressable(lv_obj_t *r, lv_event_cb_t cb, void *user = nullptr) {
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(r, c(kSurfaceHi), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_opa(r, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, user);
}

lv_obj_t *nav_row(lv_obj_t *g, bool divider, const char *sym, uint32_t sym_color, const char *text, lv_event_cb_t cb) {
  lv_obj_t *r = row(g, divider);
  icon(r, sym, sym_color);
  label(r, text);
  lv_obj_t *chev = make_label(r, font_small(), kMuted, LV_SYMBOL_RIGHT);
  lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *val = make_label(r, font_small(), kMuted, "");
  lv_obj_set_width(val, 140);
  lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(val, LV_ALIGN_RIGHT_MID, -18, 0);
  make_pressable(r, cb);
  return val;
}

lv_obj_t *switch_row(lv_obj_t *g, bool divider, const char *sym, const char *text, bool on, lv_event_cb_t cb) {
  lv_obj_t *r = row(g, divider);
  icon(r, sym, kMuted);
  lv_obj_t *l = label(r, text);
  lv_obj_set_width(l, 190);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_t *sw = lv_switch_create(r);
  lv_obj_set_size(sw, 48, 26);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(sw, c(kBorder), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw, c(kCpu), static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
  lv_obj_set_style_opa(sw, LV_OPA_40, LV_STATE_DISABLED);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return sw;
}

// ---- actions ---------------------------------------------------------------------------------------
void send_action(Action a) {
  if (!app::controls_available() || commands::send(a) == 0) ui::toast(tr(Str::ControlsOffline), kCritical);
}

void lock_cb(lv_event_t *) { send_action(Action::SessionLock); }
void screen_off_cb(lv_event_t *) { power::screen_off(); }
void deep_sleep_cb(lv_event_t *) {
  ui::confirm(tr(Str::ConfirmDeepSleep), tr(Str::DeepSleepOk), kGpu, [](void *) { power::deep_sleep(); }, nullptr);
}
void restart_cb(lv_event_t *) {
  ui::confirm(tr(Str::ConfirmRestart), tr(Str::Restart), kWarning, [](void *) { send_action(Action::SystemRestart); }, nullptr);
}
void shutdown_cb(lv_event_t *) {
  ui::confirm(tr(Str::ConfirmShutdown), tr(Str::Shutdown), kCritical, [](void *) { send_action(Action::SystemShutdown); }, nullptr);
}

void bright_cb(lv_event_t *e) {
  const int v = lv_slider_get_value(lv_event_get_target(e));
  char buf[8];
  snprintf(buf, sizeof(buf), "%d %%", v);
  set_text(s_bright_val, buf);
  app::set_brightness(static_cast<uint8_t>(v), false);
}

void bright_release_cb(lv_event_t *e) {
  app::set_brightness(static_cast<uint8_t>(lv_slider_get_value(lv_event_get_target(e))), true);
}

void lang_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  app::set_language(sel == 1 ? storage::Lang::En : storage::Lang::Es);  // rebuilds UI asynchronously
}

void refresh_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 3) app::set_refresh(kRefreshValues[sel]);
}

void units_cb(lv_event_t *e) { app::set_temp_f(lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1); }
void clock_cb(lv_event_t *e) { app::set_clock_12h(lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1); }

lv_obj_t *segmented(lv_obj_t *r, const char **map, uint16_t sel, int w, lv_event_cb_t cb) {
  lv_obj_t *m = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(m, map);
  lv_btnmatrix_set_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(m, true);
  lv_btnmatrix_set_btn_ctrl(m, sel, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(m, w, 38);
  lv_obj_align(m, LV_ALIGN_RIGHT_MID, 0, 0);
  style_segmented(m);
  lv_obj_add_event_cb(m, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return m;
}

void cur_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < storage::kCurrencyCount) app::set_crypto_currency(static_cast<uint8_t>(sel));
}
void crefresh_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 3) app::set_crypto_refresh(kCryptoRefresh[sel]);
}
void style_cb(lv_event_t *e) { app::set_crypto_style(lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1 ? 1 : 0); }

void sleep_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 5) app::set_auto_sleep(kSleepValues[sel]);
}

// ---- screensaver -----------------------------------------------------------------------------------
uint16_t index_of(const uint8_t *values, int n, uint8_t v) {
  for (int i = 0; i < n; i++)
    if (values[i] == v) return static_cast<uint16_t>(i);
  return 0;
}

template <typename F>
void ss_change(F mutate) {
  storage::Screensaver ss = app::state().settings.ss;
  mutate(ss);
  app::set_screensaver(ss);
  on_screensaver();
}

void ss_switch_cb(lv_event_t *e) {
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  ss_change([on](storage::Screensaver &ss) { ss.enabled = on; });
}
void ss_wait_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 5) ss_change([sel](storage::Screensaver &ss) { ss.wait_min = kSsWait[sel]; });
}
void ss_mode_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 3) ss_change([sel](storage::Screensaver &ss) { ss.bg_mode = static_cast<uint8_t>(sel); });
}
void ss_every_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 4) ss_change([sel](storage::Screensaver &ss) { ss.cycle_min = kSsEvery[sel]; });
}
// Multi-select chips: each label carries a check mark while it is on, so on/off is obvious.
void ss_show_labels(uint8_t mask) {
  static const Str kNames[4] = {Str::SsWeather, Str::SsPc, Str::SsMusic, Str::SsCrypto};
  static const int kSlot[4] = {0, 1, 3, 4};  // map slots around the "\n"; button ids stay 0..3
  for (int i = 0; i < 4; i++) {
    snprintf(s_ss_show_txt[i], sizeof(s_ss_show_txt[i]), "%s%s", (mask & (1u << i)) ? LV_SYMBOL_OK "  " : "",
             tr(kNames[i]));
    s_ss_show_map[kSlot[i]] = s_ss_show_txt[i];
  }
  if (s_ss_show) lv_btnmatrix_set_map(s_ss_show, s_ss_show_map);  // same button count: states are kept
}

void ss_show_cb(lv_event_t *e) {
  lv_obj_t *m = lv_event_get_target(e);
  uint8_t mask = 0;
  for (uint16_t i = 0; i < 4; i++)
    if (lv_btnmatrix_has_btn_ctrl(m, i, LV_BTNMATRIX_CTRL_CHECKED)) mask |= static_cast<uint8_t>(1u << i);
  ss_show_labels(mask);
  ss_change([mask](storage::Screensaver &ss) { ss.show = mask; });
}
void ss_bat_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 3) ss_change([sel](storage::Screensaver &ss) { ss.battery = static_cast<uint8_t>(sel); });
}
void ss_dim_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 3) ss_change([sel](storage::Screensaver &ss) { ss.dim = kSsDim[sel]; });
}

// Row with the label on top and a full-width segmented control below (like Auto sleep / Currency):
// the portrait screen has no room for both on one line.
lv_obj_t *wide_row(lv_obj_t *g, const char *sym, uint32_t color, const char *text) {
  lv_obj_t *r = row(g, true, kWideRowH);
  lv_obj_align(icon(r, sym, color), LV_ALIGN_TOP_LEFT, 0, 12);
  lv_obj_align(label(r, text), LV_ALIGN_TOP_LEFT, 34, 12);
  return r;
}

// one: a single choice (sel checked); otherwise several segments can be on (toggles).
lv_obj_t *wide_segmented(lv_obj_t *r, const char **map, bool one, uint16_t sel, lv_event_cb_t cb) {
  lv_obj_t *m = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(m, map);
  lv_btnmatrix_set_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_CHECKABLE);
  if (one) {
    lv_btnmatrix_set_one_checked(m, true);
    lv_btnmatrix_set_btn_ctrl(m, sel, LV_BTNMATRIX_CTRL_CHECKED);
  }
  lv_obj_set_size(m, LV_PCT(100), 36);
  lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, -10);
  style_segmented(m);
  lv_obj_add_event_cb(m, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return m;
}

// Segmented control (language, refresh): dark track, selected segment highlighted.
void style_segmented(lv_obj_t *m) {
  lv_obj_set_style_pad_all(m, 3, 0);
  lv_obj_set_style_pad_gap(m, 3, 0);
  lv_obj_set_style_bg_color(m, c(kBg), 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_radius(m, 10, 0);
  // Selected segment: accent-tinted fill, accent hairline and bright text; the others stay quiet.
  const lv_style_selector_t on = static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED;
  lv_obj_set_style_bg_color(m, c(kSurface), LV_PART_ITEMS);
  lv_obj_set_style_text_color(m, c(kMuted), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, lv_color_mix(c(kCpu), c(kSurface), 90), on);
  lv_obj_set_style_border_color(m, c(kCpu), on);
  lv_obj_set_style_border_width(m, 1, on);
  lv_obj_set_style_text_color(m, c(kText), on);
  lv_obj_set_style_text_font(m, font_small(), LV_PART_ITEMS);
  lv_obj_set_style_radius(m, 8, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
}

void alerts_cb(lv_event_t *e) { app::set_alerts_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)); }

void set_enabled(lv_obj_t *o, bool en) {
  if (!o) return;
  if (en)
    lv_obj_clear_state(o, LV_STATE_DISABLED);
  else
    lv_obj_add_state(o, LV_STATE_DISABLED);
}

// Segmented control on/off: no input while disabled (LVGL skips disabled objects in hit tests) and
// drawn at 40 %. The opacity is a plain local style: one set for LV_STATE_DISABLED never reaches the
// segments, which the button matrix draws with their own per-button state.
void set_segmented_enabled(lv_obj_t *m, bool en) {
  if (!m) return;
  set_enabled(m, en);
  lv_obj_set_style_opa(m, en ? LV_OPA_COVER : LV_OPA_40, 0);
}

// A screensaver row with a segmented control: the whole row (icon, label, control) reads as off.
void set_ss_row_enabled(lv_obj_t *m, bool en) {
  if (!m) return;
  set_enabled(m, en);
  lv_obj_t *r = lv_obj_get_parent(m);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(r); i++)
    lv_obj_set_style_opa(lv_obj_get_child(r, i), en ? LV_OPA_COVER : LV_OPA_40, 0);
}

// ---- tab order -------------------------------------------------------------------------------------
uint32_t tab_color(uint8_t tab) {
  switch (static_cast<storage::Tab>(tab)) {
    case storage::Tab::Thermals: return kWarning;
    case storage::Tab::Player: return kOk;
    case storage::Tab::Today: return kPower;
    case storage::Tab::Crypto: return kCrypto;
    default: return kCpu;
  }
}

void refresh_tabs() {
  if (!s_tab_lbl[0]) return;
  const uint8_t *order = app::state().settings.tab_order;
  for (int i = 0; i < storage::kTabCount; i++) {
    set_text(s_tab_icon[i], tab_icon(order[i]));
    set_color(s_tab_icon[i], tab_color(order[i]));
    set_text(s_tab_lbl[i], tr(tab_label(order[i])));
    set_enabled(s_tab_up[i], i > 0);
    set_enabled(s_tab_down[i], i < storage::kTabCount - 1);
    const bool hidden = app::state().settings.tab_hidden & (1u << order[i]);
    lv_obj_t *eye = lv_obj_get_child(s_tab_eye[i], 0);
    set_text(eye, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    set_color(eye, hidden ? kMuted : kText);
    // A hidden tab reads as switched off.
    lv_obj_set_style_opa(s_tab_icon[i], hidden ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_tab_lbl[i], hidden ? LV_OPA_40 : LV_OPA_COVER, 0);
  }
}

// user data: slot * 2 + (0 = up, 1 = down)
void tab_move_cb(lv_event_t *e) {
  const int v = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const int slot = v / 2;
  const int other = (v & 1) ? slot + 1 : slot - 1;
  if (other < 0 || other >= storage::kTabCount) return;
  uint8_t order[storage::kTabCount];
  memcpy(order, app::state().settings.tab_order, sizeof(order));
  const uint8_t t = order[slot];
  order[slot] = order[other];
  order[other] = t;
  app::set_tab_order(order);  // -> ui::apply_tab_order() -> on_tabs()
}

void tab_eye_cb(lv_event_t *e) {
  const int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const auto &cfg = app::state().settings;
  const uint8_t tab = cfg.tab_order[slot];
  app::set_tab_hidden(tab, !(cfg.tab_hidden & (1u << tab)));  // -> apply_tab_order() -> on_tabs()
}

lv_obj_t *arrow_button(lv_obj_t *r, const char *sym, int x, intptr_t data, lv_event_cb_t cb = nullptr) {
  lv_obj_t *b = lv_btn_create(r);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, 44, 36);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_bg_color(b, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_style_opa(b, LV_OPA_30, LV_STATE_DISABLED);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, x, 0);
  lv_obj_t *l = make_label(b, font_small(), kText, sym);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb ? cb : tab_move_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(data));
  return b;
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  const auto &cfg = app::state().settings;
  lv_obj_t *page = make_page(parent, true);
  lv_obj_set_style_pad_row(page, 6, 0);

  // CONEXIÓN
  lv_obj_t *g = section(page, tr(Str::SectionConnection));
  s_wifi_val = nav_row(g, false, LV_SYMBOL_WIFI, kCpu, tr(Str::WifiTitle), [](lv_event_t *) { navigate(Page::Wifi); });
  s_pc_val = nav_row(g, true, LV_SYMBOL_SHUFFLE, kGpu, tr(Str::PairButton), [](lv_event_t *) { navigate(Page::Pair); });
  nav_row(g, true, PCD_SYM_SLIDERS, kPower, tr(Str::WizSettingsRow), [](lv_event_t *) { ui::setup::open(); });

  // SISTEMA
  g = section(page, tr(Str::SystemTitle));
  // This deck
  lv_obj_t *r = row(g, false);
  icon(r, LV_SYMBOL_EYE_CLOSE, kCpu);
  label(r, tr(Str::ScreenOff));
  make_pressable(r, screen_off_cb);
  r = row(g, true);
  icon(r, PCD_SYM_MOON, kGpu);
  label(r, tr(Str::DeepSleep));
  make_pressable(r, deep_sleep_cb);
  // Auto sleep: label on top, segmented control below.
  r = row(g, true, kWideRowH);
  s_sleep_row = r;
  lv_obj_t *ai = icon(r, LV_SYMBOL_POWER, kMuted);
  lv_obj_align(ai, LV_ALIGN_TOP_LEFT, 0, 12);
  lv_obj_t *al = label(r, tr(Str::AutoSleep));
  lv_obj_align(al, LV_ALIGN_TOP_LEFT, 34, 12);
  s_sleep_lbl = al;
  s_sleep_note = make_label(r, font_small(), kMuted, tr(Str::SsReplacedBySaver));
  lv_obj_set_width(s_sleep_note, 238);
  lv_label_set_long_mode(s_sleep_note, LV_LABEL_LONG_DOT);
  lv_obj_align(s_sleep_note, LV_ALIGN_TOP_LEFT, 34, 37);
  lv_obj_add_flag(s_sleep_note, LV_OBJ_FLAG_HIDDEN);
  s_sleep_map[4] = tr(Str::NeverOption);
  lv_obj_t *sl = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(sl, s_sleep_map);
  lv_btnmatrix_set_btn_ctrl_all(sl, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(sl, true);
  uint16_t sel = 4;
  for (uint16_t i = 0; i < 5; i++)
    if (kSleepValues[i] == cfg.auto_sleep_min) sel = i;
  lv_btnmatrix_set_btn_ctrl(sl, sel, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(sl, LV_PCT(100), 36);
  lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -10);
  style_segmented(sl);
  lv_obj_add_event_cb(sl, sleep_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  s_sleep_m = sl;
  // The PC
  g = group(page);
  s_lock = row(g, false);
  icon(s_lock, PCD_SYM_LOCK, kCpu);
  label(s_lock, tr(Str::LockPc));
  make_pressable(s_lock, lock_cb);
  s_restart = row(g, true);
  icon(s_restart, LV_SYMBOL_REFRESH, kWarning);
  label(s_restart, tr(Str::RestartPc));
  make_pressable(s_restart, restart_cb);
  s_shutdown = row(g, true);
  icon(s_shutdown, LV_SYMBOL_POWER, kCritical);
  label(s_shutdown, tr(Str::ShutdownPc), kCritical);
  make_pressable(s_shutdown, shutdown_cb);
  s_power_hint = make_label(page, font_small(), kMuted, tr(Str::PowerHint));
  lv_obj_set_width(s_power_hint, LV_PCT(100));
  lv_label_set_long_mode(s_power_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_hor(s_power_hint, 6, 0);

  // PANTALLA
  g = section(page, tr(Str::SectionDisplay));
  r = row(g, false, 56);
  icon(r, LV_SYMBOL_IMAGE, kPower);
  s_bright = lv_slider_create(r);
  style_slider(s_bright, kPower);
  lv_obj_set_size(s_bright, 160, 8);
  lv_obj_align(s_bright, LV_ALIGN_LEFT_MID, 40, 0);
  lv_slider_set_range(s_bright, 5, 100);
  lv_slider_set_value(s_bright, cfg.brightness, LV_ANIM_OFF);
  lv_obj_add_event_cb(s_bright, bright_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_bright, bright_release_cb, LV_EVENT_RELEASED, nullptr);
  s_bright_val = make_label(r, font_small(), kMuted, "");
  lv_obj_align(s_bright_val, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_label_set_text_fmt(s_bright_val, "%d %%", cfg.brightness);
  r = row(g, true, 56);
  icon(r, LV_SYMBOL_KEYBOARD, kMuted);
  s_lang = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(s_lang, s_lang_map);
  lv_btnmatrix_set_btn_ctrl_all(s_lang, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(s_lang, true);
  lv_btnmatrix_set_btn_ctrl(s_lang, cfg.lang == storage::Lang::En ? 1 : 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(s_lang, 220, 38);
  lv_obj_align(s_lang, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_pad_all(s_lang, 3, 0);
  lv_obj_set_style_pad_gap(s_lang, 3, 0);
  lv_obj_set_style_bg_color(s_lang, c(kBg), 0);
  lv_obj_set_style_border_width(s_lang, 0, 0);
  lv_obj_set_style_radius(s_lang, 10, 0);
  lv_obj_set_style_bg_color(s_lang, c(kSurface), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_lang, c(kBorder), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(s_lang, c(kText), LV_PART_ITEMS);
  lv_obj_set_style_text_font(s_lang, font_small(), LV_PART_ITEMS);
  lv_obj_set_style_radius(s_lang, 8, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(s_lang, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(s_lang, lang_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  r = row(g, true, 56);
  icon(r, LV_SYMBOL_REFRESH, kMuted);
  label(r, tr(Str::Refresh));
  s_refresh = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(s_refresh, s_refresh_map);
  lv_btnmatrix_set_btn_ctrl_all(s_refresh, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(s_refresh, true);
  lv_btnmatrix_set_btn_ctrl(s_refresh, cfg.refresh_s == 1 ? 0 : cfg.refresh_s == 2 ? 1 : 2, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(s_refresh, 150, 38);
  lv_obj_align(s_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
  style_segmented(s_refresh);
  lv_obj_add_event_cb(s_refresh, refresh_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  // SALVAPANTALLAS
  const storage::Screensaver &ss = cfg.ss;
  g = section(page, tr(Str::SectionScreensaver));
  s_ss_switch = switch_row(g, false, LV_SYMBOL_IMAGE, tr(Str::Screensaver), ss.enabled, ss_switch_cb);
  r = wide_row(g, LV_SYMBOL_BELL, kMuted, tr(Str::SsAfter));
  s_ss_wait = wide_segmented(r, s_ss_wait_map, true, index_of(kSsWait, 5, ss.wait_min), ss_wait_cb);
  r = wide_row(g, LV_SYMBOL_SHUFFLE, kGpu, tr(Str::SsBackground));
  s_ss_mode_map[0] = tr(Str::SsFixed);
  s_ss_mode_map[1] = tr(Str::SsRandom);
  s_ss_mode_map[2] = tr(Str::SsCycle);
  s_ss_mode = wide_segmented(r, s_ss_mode_map, true, ss.bg_mode <= 2 ? ss.bg_mode : 0, ss_mode_cb);
  r = wide_row(g, LV_SYMBOL_LOOP, kMuted, tr(Str::SsEvery));
  s_ss_every = wide_segmented(r, s_ss_every_map, true, index_of(kSsEvery, 4, ss.cycle_min), ss_every_cb);
  s_ss_img_val = nav_row(g, true, LV_SYMBOL_IMAGE, kCpu, tr(Str::SsImage), [](lv_event_t *) { screensaver::open_picker(); });
  s_ss_img_row = lv_obj_get_parent(s_ss_img_val);
  r = wide_row(g, LV_SYMBOL_LIST, kMuted, tr(Str::SsShow));
  s_ss_show = nullptr;
  ss_show_labels(ss.show);
  s_ss_show = wide_segmented(r, s_ss_show_map, false, 0, ss_show_cb);  // several can be on
  lv_obj_set_height(r, kWideRowH + 34);  // two rows of chips
  lv_obj_set_height(s_ss_show, 70);
  for (uint16_t i = 0; i < 4; i++)
    if (ss.show & (1u << i)) lv_btnmatrix_set_btn_ctrl(s_ss_show, i, LV_BTNMATRIX_CTRL_CHECKED);
  // Report on release, after the button toggled: by default the matrix sends VALUE_CHANGED on press,
  // before the toggle, so the stored mask lagged one tap behind what the chips showed.
  lv_btnmatrix_set_btn_ctrl_all(s_ss_show, LV_BTNMATRIX_CTRL_CLICK_TRIG);
  r = row(g, true, 56);
  icon(r, LV_SYMBOL_EYE_OPEN, kPower);
  label(r, tr(Str::SsDim));
  // 136 px leaves room for "Brightness"; "100 %" gets a wider segment (4:3:3).
  s_ss_dim = segmented(r, s_ss_dim_map, index_of(kSsDim, 3, ss.dim), 136, ss_dim_cb);
  lv_btnmatrix_set_btn_width(s_ss_dim, 0, 4);
  lv_btnmatrix_set_btn_width(s_ss_dim, 1, 3);
  lv_btnmatrix_set_btn_width(s_ss_dim, 2, 3);
  s_ss_bat = nullptr;
  if (battery::sensed()) {  // the deck's battery in the screensaver's corner (never on USB)
    s_ss_bat_map[0] = tr(Str::SsBatOff);
    s_ss_bat_map[1] = tr(Str::SsBatIcon);
    s_ss_bat_map[2] = tr(Str::SsBatPct);
    r = wide_row(g, LV_SYMBOL_BATTERY_3, kOk, tr(Str::SsBattery));
    s_ss_bat = wide_segmented(r, s_ss_bat_map, true, ss.battery <= 2 ? ss.battery : 2, ss_bat_cb);
  }
  s_ss_preview = row(g, true);
  icon(s_ss_preview, LV_SYMBOL_PLAY, kOk);
  label(s_ss_preview, tr(Str::SsPreview));
  make_pressable(s_ss_preview, [](lv_event_t *) { screensaver::start(); });
  lv_obj_t *ss_hint = make_label(page, font_small(), kMuted, tr(Str::SsHint));
  lv_obj_set_width(ss_hint, LV_PCT(100));
  lv_label_set_long_mode(ss_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_hor(ss_hint, 6, 0);
  on_screensaver();

  // HORA Y TIEMPO
  g = section(page, tr(Str::SectionWeather));
  s_place_val = nav_row(g, false, PCD_SYM_PIN, kCpu, tr(Str::Cities), [](lv_event_t *) { cities::open(); });
  r = row(g, true, 56);
  icon(r, PCD_SYM_THERMO, kWarning);
  label(r, tr(Str::Units));
  s_units = segmented(r, s_units_map, cfg.temp_f ? 1 : 0, 120, units_cb);
  r = row(g, true, 56);
  icon(r, PCD_SYM_CLOCK, kGpu);
  label(r, tr(Str::ClockFormat));
  s_clock = segmented(r, s_clock_map, cfg.clock_12h ? 1 : 0, 120, clock_cb);
  switch_row(g, true, LV_SYMBOL_IMAGE, tr(Str::TodayBg), cfg.today_bg,
             [](lv_event_t *e) { app::set_today_bg(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)); });
  on_place();

  // CRIPTO
  g = section(page, tr(Str::SectionCrypto));
  s_coins_val = nav_row(g, false, PCD_SYM_COINS, kCrypto, tr(Str::CryCoinsTitle), [](lv_event_t *) { coins::open(); });
  // Currency: label on top, the six currencies below (full width).
  r = row(g, true, 92);
  lv_obj_t *ci = icon(r, LV_SYMBOL_SHUFFLE, kOk);
  lv_obj_align(ci, LV_ALIGN_TOP_LEFT, 0, 12);
  lv_obj_t *cl = label(r, tr(Str::Currency));
  lv_obj_align(cl, LV_ALIGN_TOP_LEFT, 34, 12);
  lv_obj_t *cm = segmented(r, s_cur_map, cfg.crypto_cur < storage::kCurrencyCount ? cfg.crypto_cur : 1, 0, cur_cb);
  lv_obj_set_size(cm, LV_PCT(100), 36);
  lv_obj_align(cm, LV_ALIGN_BOTTOM_MID, 0, -10);
  r = row(g, true, 56);
  icon(r, LV_SYMBOL_REFRESH, kMuted);
  label(r, tr(Str::CryRefresh));
  segmented(r, s_crefresh_map, cfg.crypto_refresh_s == 15 ? 0 : cfg.crypto_refresh_s == 60 ? 2 : 1, 132, crefresh_cb);
  r = row(g, true, 56);
  icon(r, PCD_SYM_CHART_LINE, kCpu);
  label(r, tr(Str::ChartStyle));
  s_style_map[0] = tr(Str::StyleLine);
  s_style_map[1] = tr(Str::StyleCandles);
  segmented(r, s_style_map, cfg.crypto_style == 1 ? 1 : 0, 150, style_cb);
  on_crypto();

  // PESTAÑAS
  g = section(page, tr(Str::SectionTabs));
  for (int i = 0; i < storage::kTabCount; i++) {
    lv_obj_t *tr_row = row(g, i > 0, 52);
    s_tab_icon[i] = icon(tr_row, "", kCpu);
    s_tab_lbl[i] = label(tr_row, "");
    s_tab_up[i] = arrow_button(tr_row, LV_SYMBOL_UP, -50, i * 2);
    s_tab_down[i] = arrow_button(tr_row, LV_SYMBOL_DOWN, 0, i * 2 + 1);
    s_tab_eye[i] = arrow_button(tr_row, LV_SYMBOL_EYE_OPEN, -112, i, tab_eye_cb);
  }
  refresh_tabs();
  lv_obj_t *tabs_hint = make_label(page, font_small(), kMuted, tr(Str::TabsHint));
  lv_obj_set_width(tabs_hint, LV_PCT(100));
  lv_label_set_long_mode(tabs_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_hor(tabs_hint, 6, 0);

  // OPCIONES
  g = section(page, tr(Str::SectionOptions));
  s_alerts = switch_row(g, false, LV_SYMBOL_WARNING, tr(Str::Alerts), cfg.alerts_enabled, alerts_cb);

  // INFORMACIÓN
  g = section(page, tr(Str::SectionInfo));
  const char *keys[7] = {tr(Str::Firmware), "IP",          "Wi-Fi",         tr(Str::DeckBattery),
                         tr(Str::HeapInternal), tr(Str::Psram), tr(Str::Uptime)};
  for (int i = 0; i < 7; i++) {
    lv_obj_t *ir = row(g, i > 0, 36);
    lv_obj_t *k = make_label(ir, font_small(), kMuted, keys[i]);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
    s_info[i] = make_label(ir, font_small(), kText, "");
    lv_obj_align(s_info[i], LV_ALIGN_RIGHT_MID, 0, 0);
  }

  update();
  tick();
  return page;
}

void on_tabs() { refresh_tabs(); }

void on_screensaver() {
  if (!s_ss_switch) return;
  const storage::Screensaver &ss = app::state().settings.ss;
  if (ss.enabled != lv_obj_has_state(s_ss_switch, LV_STATE_CHECKED)) {
    if (ss.enabled)
      lv_obj_add_state(s_ss_switch, LV_STATE_CHECKED);
    else
      lv_obj_clear_state(s_ss_switch, LV_STATE_CHECKED);
  }
  // The choices may also change from the USB console: keep the controls in step.
  auto pick = [](lv_obj_t *m, uint16_t i) {
    if (!lv_btnmatrix_has_btn_ctrl(m, i, LV_BTNMATRIX_CTRL_CHECKED)) lv_btnmatrix_set_btn_ctrl(m, i, LV_BTNMATRIX_CTRL_CHECKED);
  };
  pick(s_ss_wait, index_of(kSsWait, 5, ss.wait_min));
  pick(s_ss_mode, ss.bg_mode <= 2 ? ss.bg_mode : 0);
  pick(s_ss_every, index_of(kSsEvery, 4, ss.cycle_min));
  pick(s_ss_dim, index_of(kSsDim, 3, ss.dim));
  if (s_ss_bat) pick(s_ss_bat, ss.battery <= 2 ? ss.battery : 2);
  for (uint16_t i = 0; i < 4; i++) {
    const bool on = ss.show & (1u << i);
    if (on != lv_btnmatrix_has_btn_ctrl(s_ss_show, i, LV_BTNMATRIX_CTRL_CHECKED)) {
      if (on)
        lv_btnmatrix_set_btn_ctrl(s_ss_show, i, LV_BTNMATRIX_CTRL_CHECKED);
      else
        lv_btnmatrix_clear_btn_ctrl(s_ss_show, i, LV_BTNMATRIX_CTRL_CHECKED);
    }
  }
  ss_show_labels(ss.show);  // the check marks follow the stored choice
  for (lv_obj_t *m : {s_ss_wait, s_ss_mode, s_ss_show, s_ss_dim}) set_ss_row_enabled(m, ss.enabled);
  if (s_ss_bat) set_ss_row_enabled(s_ss_bat, ss.enabled);
  set_ss_row_enabled(s_ss_every, ss.enabled && ss.bg_mode == static_cast<uint8_t>(storage::SsBg::Cycle));
  set_enabled(s_ss_img_row, ss.enabled);  // nav row: dimmed by its own DISABLED style
  set_text(s_ss_img_val, ui::wallpaper::name(ss.bg));
  // Auto sleep is replaced while the screensaver is on: greyed out, with a note under its name.
  set_segmented_enabled(s_sleep_m, !ss.enabled);
  set_color(s_sleep_lbl, ss.enabled ? kMuted : kText);
  if (ss.enabled)
    lv_obj_clear_flag(s_sleep_note, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(s_sleep_note, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(s_sleep_row, ss.enabled ? kSleepNoteRowH : kWideRowH);
}

void on_crypto() {
  if (!s_coins_val) return;
  int8_t idx[crypto::kMaxCoins];
  const int n = crypto::parse_watch(app::state().settings.crypto_coins, idx, crypto::kMaxCoins);
  char buf[64];
  char list[40];
  crypto::format_watch(idx, n < 3 ? n : 3, list, sizeof(list));
  for (char *c = list; *c; c++)
    if (*c == ',') *c = ' ';
  if (n == 0)
    snprintf(buf, sizeof(buf), "%s", tr(Str::LocationNone));
  else if (n <= 3)
    snprintf(buf, sizeof(buf), "%s", list);
  else
    snprintf(buf, sizeof(buf), "%s +%d", list, n - 3);
  set_text(s_coins_val, buf);
}

void on_place() {
  if (!s_place_val) return;
  const auto &cfg = app::state().settings;
  char place[56];  // the main place, and how many more cities Today has
  if (!cfg.place_name[0])
    snprintf(place, sizeof(place), "%s", tr(Str::LocationNone));
  else if (cfg.more_count)
    snprintf(place, sizeof(place), "%s +%d", cfg.place_name, cfg.more_count);
  else
    snprintf(place, sizeof(place), "%s", cfg.place_name);
  set_text(s_place_val, place);
  // Units / clock may also change from the setup wizard: keep the segmented controls in step.
  const uint16_t u = cfg.temp_f ? 1 : 0, k = cfg.clock_12h ? 1 : 0;
  if (s_units && !lv_btnmatrix_has_btn_ctrl(s_units, u, LV_BTNMATRIX_CTRL_CHECKED))
    lv_btnmatrix_set_btn_ctrl(s_units, u, LV_BTNMATRIX_CTRL_CHECKED);
  if (s_clock && !lv_btnmatrix_has_btn_ctrl(s_clock, k, LV_BTNMATRIX_CTRL_CHECKED))
    lv_btnmatrix_set_btn_ctrl(s_clock, k, LV_BTNMATRIX_CTRL_CHECKED);
}

void update() {
  if (!s_lock) return;
  const auto &st = app::state();
  const bool online = app::controls_available() && !st.settings.demo_mode;
  set_enabled(s_lock, online && st.caps.lock);
  set_enabled(s_restart, online && st.caps.power);
  set_enabled(s_shutdown, online && st.caps.power);
  if (online && !st.caps.power)
    lv_obj_clear_flag(s_power_hint, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(s_power_hint, LV_OBJ_FLAG_HIDDEN);
}

void tick() {
  if (!s_info[0] || current_page() != Page::Settings) return;  // each repaint is a full-frame push
  const auto &st = app::state();
  const net::Status ns = net::status();
  char buf[64];

  // Connection rows
  if (st.settings.demo_mode)
    snprintf(buf, sizeof(buf), "%s", tr(Str::StateDemo));
  else if (ns.wifi_connected)
    snprintf(buf, sizeof(buf), "%s", ns.ip);
  else
    snprintf(buf, sizeof(buf), "%s", tr(Str::NotConnected));
  set_text(s_wifi_val, buf);
  if (ns.paired && ns.agent_name[0])
    snprintf(buf, sizeof(buf), "%s%s", ns.agent_name, st.link == app::LinkState::Online ? "  " LV_SYMBOL_OK : "");
  else
    snprintf(buf, sizeof(buf), "%s", tr(Str::PairNotPaired));
  set_text(s_pc_val, buf);

  // Info
  set_text(s_info[0], PCD_FW_VERSION);
  set_text(s_info[1], ns.ip[0] ? ns.ip : "--");
  if (ns.wifi_connected)
    snprintf(buf, sizeof(buf), "%d dBm", static_cast<int>(ns.rssi));
  else
    snprintf(buf, sizeof(buf), "--");
  set_text(s_info[2], buf);
  const battery::State bat = battery::state();
  if (bat.source == battery::Source::Usb && battery::sensed() && bat.mv > 0)  // the voltage too: shows what the deck measures
    snprintf(buf, sizeof(buf), "%s %s · %.2f V", LV_SYMBOL_CHARGE, tr(Str::DeckOnUsb), static_cast<double>(bat.mv) / 1000.0);
  else if (bat.source == battery::Source::Usb)
    snprintf(buf, sizeof(buf), "%s %s", LV_SYMBOL_CHARGE, tr(Str::DeckOnUsb));
  else if (bat.source == battery::Source::Battery)
    snprintf(buf, sizeof(buf), "%d %% · %.2f V", bat.pct, static_cast<double>(bat.mv) / 1000.0);
  else
    snprintf(buf, sizeof(buf), "--");
  set_text(s_info[3], buf);
  snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  set_text(s_info[4], buf);
  snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  set_text(s_info[5], buf);
  const uint32_t up = millis() / 1000;
  snprintf(buf, sizeof(buf), "%uh %02um", static_cast<unsigned>(up / 3600), static_cast<unsigned>((up / 60) % 60));
  set_text(s_info[6], buf);
}

}  // namespace settings
}  // namespace ui
