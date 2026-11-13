// Settings, grouped like a native settings app:
//   CONEXIÓN   Wi-Fi ›, PC vinculado ›
//   SISTEMA    deck: Apagar pantalla, Reposo automático (screen off; a touch or BOOT wakes it) · PC:
//              Bloquear, Reiniciar, Apagar
//              (confirmation; PC shutdown/restart only if the PC app allows it)
//   PANTALLA   brightness, language
//   SALVAPANTALLAS on/off, start after, background (fixed / random / rotate), change period, image
//              (picker sheet), data shown (weather, PC, music, crypto), brightness, preview
//   HORA Y TIEMPO weather place (city search), °C/°F, 24/12 h
//   CRIPTO     coins (picker sheet), currency, price refresh, chart style
//   PESTAÑAS   order and visibility of the tabs in the navigation rail (stored; first visible opens at boot)
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
#include "ui/crypto_fmt.h"
#include "esp_heap_caps.h"
#include "network/net_task.h"
#include "ui/ui.h"
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
lv_obj_t *s_coins_val = nullptr;
const char *s_cur_map[] = {"USD", "EUR", "MXN", "ARS", "COP", "BRL", ""};
const char *s_crefresh_map[] = {"15 s", "30 s", "60 s", ""};
constexpr uint8_t kCryptoRefresh[] = {15, 30, 60};
const char *s_style_map[] = {"", "", ""};   // Line / Candles (i18n)
const char *s_units_map[] = {"°C", "°F", ""};
const char *s_clock_map[] = {"24 h", "12 h", ""};
const char *s_sleep_map[] = {"1 min", "2 min", "5 min", "10 min", "", ""};  // [4] = Never (i18n)
constexpr uint8_t kSleepValues[] = {1, 2, 5, 10, 0};
lv_obj_t *s_sleep_m = nullptr;    // auto-sleep control (disabled while the screensaver is on)
lv_obj_t *s_sleep_lbl = nullptr;
// Screensaver
const char *s_ss_wait_map[] = {"1 min", "2 min", "5 min", "10 min", "30 min", ""};
constexpr uint8_t kSsWait[] = {1, 2, 5, 10, 30};
const char *s_ss_mode_map[] = {"", "", "", ""};       // Fixed / Random / Rotate (i18n)
const char *s_ss_every_map[] = {"1 min", "5 min", "15 min", "30 min", ""};
constexpr uint8_t kSsEvery[] = {1, 5, 15, 30};
const char *s_ss_show_map[] = {"", "", "", "", ""};   // Weather / PC / Music / Crypto (i18n, "✓ " when on)
char s_ss_show_txt[4][32];
const char *s_ss_dim_map[] = {"100 %", "60 %", "30 %", ""};
constexpr uint8_t kSsDim[] = {100, 60, 30};
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
  lv_obj_set_style_pad_ver(g, 3, 0);
  lv_obj_set_style_pad_hor(g, 18, 0);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_COLUMN);
  return g;
}

lv_obj_t *section(lv_obj_t *page, const char *title) {
  lv_obj_t *h = make_label(page, font_tiny(), kMuted, title);
  lv_obj_set_style_text_letter_space(h, 2, 0);
  lv_obj_set_style_pad_left(h, 9, 0);
  lv_obj_set_style_pad_top(h, 6, 0);
  lv_obj_t *g = make_card(page);
  lv_obj_set_size(g, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(g, 3, 0);
  lv_obj_set_style_pad_hor(g, 18, 0);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_COLUMN);
  return g;
}

lv_obj_t *row(lv_obj_t *group, bool divider = true, int h = 54) {
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
  lv_obj_t *i = make_label(r, font_small(), color, sym);
  lv_obj_set_width(i, 30);
  lv_obj_set_style_text_align(i, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(i, LV_ALIGN_LEFT_MID, 0, 0);
  return i;
}

lv_obj_t *label(lv_obj_t *r, const char *text, uint32_t color = kText) {
  lv_obj_t *l = make_label(r, font_small(), color, text);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 42, 0);
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
  lv_obj_t *chev = make_label(r, font_tiny(), kMuted, LV_SYMBOL_RIGHT);
  lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *val = make_label(r, font_tiny(), kMuted, "");
  lv_obj_set_width(val, 285);
  lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(val, LV_ALIGN_RIGHT_MID, -27, 0);
  make_pressable(r, cb);
  return val;
}

lv_obj_t *switch_row(lv_obj_t *g, bool divider, const char *sym, const char *text, bool on, lv_event_cb_t cb) {
  lv_obj_t *r = row(g, divider);
  icon(r, sym, kMuted);
  lv_obj_t *l = label(r, text);
  lv_obj_set_width(l, 420);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_t *sw = lv_switch_create(r);
  lv_obj_set_size(sw, 60, 33);
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
  lv_obj_set_size(m, w, 42);
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
  for (int i = 0; i < 4; i++) {
    snprintf(s_ss_show_txt[i], sizeof(s_ss_show_txt[i]), "%s%s", (mask & (1u << i)) ? LV_SYMBOL_OK "  " : "",
             tr(kNames[i]));
    s_ss_show_map[i] = s_ss_show_txt[i];
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
void ss_dim_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  if (sel < 3) ss_change([sel](storage::Screensaver &ss) { ss.dim = kSsDim[sel]; });
}

// Label on top, full-width segmented control below (auto sleep, screensaver delay / data).
lv_obj_t *wide_segmented(lv_obj_t *r, const char **map) {
  lv_obj_t *m = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(m, map);
  lv_btnmatrix_set_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_obj_set_size(m, LV_PCT(100), 42);
  lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, -6);
  style_segmented(m);
  return m;
}

// Segmented control (language, refresh): dark track, selected segment highlighted.
void style_segmented(lv_obj_t *m) {
  lv_obj_set_style_pad_all(m, 5, 0);
  lv_obj_set_style_pad_gap(m, 5, 0);
  lv_obj_set_style_bg_color(m, c(kBg), 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_radius(m, 15, 0);
  // Selected segment: accent-tinted fill, accent hairline and bright text; the others stay quiet.
  const lv_style_selector_t on = static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED;
  lv_obj_set_style_bg_color(m, c(kSurface), LV_PART_ITEMS);
  lv_obj_set_style_text_color(m, c(kMuted), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, lv_color_mix(c(kCpu), c(kSurface), 90), on);
  lv_obj_set_style_border_color(m, c(kCpu), on);
  lv_obj_set_style_border_width(m, 1, on);
  lv_obj_set_style_text_color(m, c(kText), on);
  lv_obj_set_style_text_font(m, font_tiny(), LV_PART_ITEMS);
  lv_obj_set_style_radius(m, 12, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
  // Disabled buttons (set per button: the button matrix ignores the object's own state).
  const lv_style_selector_t dis = static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_DISABLED;
  lv_obj_set_style_text_opa(m, LV_OPA_50, dis);
  lv_obj_set_style_bg_opa(m, LV_OPA_40, dis);
}

void set_matrix_enabled(lv_obj_t *m, bool en) {
  if (!m) return;
  if (en) {
    lv_obj_clear_state(m, LV_STATE_DISABLED);
    lv_btnmatrix_clear_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_DISABLED);
  } else {
    lv_obj_add_state(m, LV_STATE_DISABLED);
    lv_btnmatrix_set_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_DISABLED);
  }
}

void alerts_cb(lv_event_t *e) { app::set_alerts_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)); }

void set_enabled(lv_obj_t *o, bool en) {
  if (!o) return;
  if (en)
    lv_obj_clear_state(o, LV_STATE_DISABLED);
  else
    lv_obj_add_state(o, LV_STATE_DISABLED);
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
  lv_obj_set_size(b, 51, 39);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_bg_color(b, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_style_opa(b, LV_OPA_30, LV_STATE_DISABLED);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, x, 0);
  lv_obj_t *l = make_label(b, font_tiny(), kText, sym);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb ? cb : tab_move_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(data));
  return b;
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  const auto &cfg = app::state().settings;
  lv_obj_t *page = make_page(parent, true);
  lv_obj_set_style_pad_row(page, 9, 0);

  // CONEXIÓN
  lv_obj_t *g = section(page, tr(Str::SectionConnection));
  s_wifi_val = nav_row(g, false, LV_SYMBOL_WIFI, kCpu, tr(Str::WifiTitle), [](lv_event_t *) { navigate(Page::Wifi); });
  s_pc_val = nav_row(g, true, LV_SYMBOL_SHUFFLE, kGpu, tr(Str::PairButton), [](lv_event_t *) { navigate(Page::Pair); });
  nav_row(g, true, LV_SYMBOL_LIST, kOk, tr(Str::SetupRow), [](lv_event_t *) { setup::open(); });

  // SISTEMA
  g = section(page, tr(Str::SystemTitle));
  // This deck
  lv_obj_t *r = row(g, false);
  icon(r, LV_SYMBOL_EYE_CLOSE, kCpu);
  label(r, tr(Str::ScreenOff));
  make_pressable(r, screen_off_cb);
  // Auto sleep: label on top, 5-way segmented control below (full width).
  r = row(g, true, 96);
  lv_obj_t *ai = icon(r, LV_SYMBOL_POWER, kMuted);
  lv_obj_align(ai, LV_ALIGN_TOP_LEFT, 0, 9);
  lv_obj_t *al = label(r, tr(Str::AutoSleep));
  lv_obj_align(al, LV_ALIGN_TOP_LEFT, 42, 9);
  s_sleep_lbl = al;
  s_sleep_map[4] = tr(Str::NeverOption);
  lv_obj_t *sl = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(sl, s_sleep_map);
  lv_btnmatrix_set_btn_ctrl_all(sl, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(sl, true);
  uint16_t sel = 4;
  for (uint16_t i = 0; i < 5; i++)
    if (kSleepValues[i] == cfg.auto_sleep_min) sel = i;
  lv_btnmatrix_set_btn_ctrl(sl, sel, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(sl, LV_PCT(100), 42);
  lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -6);
  style_segmented(sl);
  lv_obj_set_style_opa(sl, LV_OPA_40, LV_STATE_DISABLED);
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
  s_power_hint = make_label(page, font_tiny(), kMuted, tr(Str::PowerHint));
  lv_obj_set_width(s_power_hint, LV_PCT(100));
  lv_label_set_long_mode(s_power_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_hor(s_power_hint, 9, 0);

  // PANTALLA
  g = section(page, tr(Str::SectionDisplay));
  r = row(g, false);
  icon(r, LV_SYMBOL_IMAGE, kPower);
  s_bright = lv_slider_create(r);
  style_slider(s_bright, kPower);
  lv_obj_set_size(s_bright, 390, 12);
  lv_obj_align(s_bright, LV_ALIGN_LEFT_MID, 60, 0);
  lv_slider_set_range(s_bright, 5, 100);
  lv_slider_set_value(s_bright, cfg.brightness, LV_ANIM_OFF);
  lv_obj_add_event_cb(s_bright, bright_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_bright, bright_release_cb, LV_EVENT_RELEASED, nullptr);
  s_bright_val = make_label(r, font_tiny(), kMuted, "");
  lv_obj_align(s_bright_val, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_label_set_text_fmt(s_bright_val, "%d %%", cfg.brightness);
  r = row(g, true);
  icon(r, LV_SYMBOL_KEYBOARD, kMuted);
  label(r, tr(Str::Language));
  s_lang = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(s_lang, s_lang_map);
  lv_btnmatrix_set_btn_ctrl_all(s_lang, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(s_lang, true);
  lv_btnmatrix_set_btn_ctrl(s_lang, cfg.lang == storage::Lang::En ? 1 : 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(s_lang, 270, 42);
  lv_obj_align(s_lang, LV_ALIGN_RIGHT_MID, 0, 0);
  style_segmented(s_lang);
  lv_obj_add_event_cb(s_lang, lang_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  r = row(g, true);
  icon(r, LV_SYMBOL_REFRESH, kMuted);
  label(r, tr(Str::Refresh));
  s_refresh = lv_btnmatrix_create(r);
  lv_btnmatrix_set_map(s_refresh, s_refresh_map);
  lv_btnmatrix_set_btn_ctrl_all(s_refresh, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(s_refresh, true);
  lv_btnmatrix_set_btn_ctrl(s_refresh, cfg.refresh_s == 1 ? 0 : cfg.refresh_s == 2 ? 1 : 2, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(s_refresh, 195, 42);
  lv_obj_align(s_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
  style_segmented(s_refresh);
  lv_obj_add_event_cb(s_refresh, refresh_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  // SALVAPANTALLAS
  const storage::Screensaver &ss = cfg.ss;
  g = section(page, tr(Str::SectionScreensaver));
  s_ss_switch = switch_row(g, false, LV_SYMBOL_IMAGE, tr(Str::Screensaver), ss.enabled, ss_switch_cb);
  r = row(g, true, 96);
  lv_obj_align(icon(r, LV_SYMBOL_BELL, kMuted), LV_ALIGN_TOP_LEFT, 0, 9);
  lv_obj_align(label(r, tr(Str::SsAfter)), LV_ALIGN_TOP_LEFT, 42, 9);
  s_ss_wait = wide_segmented(r, s_ss_wait_map);
  lv_btnmatrix_set_one_checked(s_ss_wait, true);
  lv_btnmatrix_set_btn_ctrl(s_ss_wait, index_of(kSsWait, 5, ss.wait_min), LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_add_event_cb(s_ss_wait, ss_wait_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  r = row(g, true);
  icon(r, LV_SYMBOL_SHUFFLE, kGpu);
  label(r, tr(Str::SsBackground));
  s_ss_mode_map[0] = tr(Str::SsFixed);
  s_ss_mode_map[1] = tr(Str::SsRandom);
  s_ss_mode_map[2] = tr(Str::SsCycle);
  s_ss_mode = segmented(r, s_ss_mode_map, ss.bg_mode <= 2 ? ss.bg_mode : 0, 420, ss_mode_cb);
  r = row(g, true);
  icon(r, LV_SYMBOL_LOOP, kMuted);
  label(r, tr(Str::SsEvery));
  s_ss_every = segmented(r, s_ss_every_map, index_of(kSsEvery, 4, ss.cycle_min), 420, ss_every_cb);
  s_ss_img_val = nav_row(g, true, LV_SYMBOL_IMAGE, kCpu, tr(Str::SsImage), [](lv_event_t *) { screensaver::open_picker(); });
  s_ss_img_row = lv_obj_get_parent(s_ss_img_val);
  r = row(g, true, 96);
  lv_obj_align(icon(r, LV_SYMBOL_LIST, kMuted), LV_ALIGN_TOP_LEFT, 0, 9);
  lv_obj_align(label(r, tr(Str::SsShow)), LV_ALIGN_TOP_LEFT, 42, 9);
  s_ss_show = nullptr;
  ss_show_labels(ss.show);
  s_ss_show = wide_segmented(r, s_ss_show_map);  // several can be on
  for (uint16_t i = 0; i < 4; i++)
    if (ss.show & (1u << i)) lv_btnmatrix_set_btn_ctrl(s_ss_show, i, LV_BTNMATRIX_CTRL_CHECKED);
  // Report on release, after the button toggled: by default the matrix sends VALUE_CHANGED on press,
  // before the toggle, so the stored mask lagged one tap behind what the chips showed.
  lv_btnmatrix_set_btn_ctrl_all(s_ss_show, LV_BTNMATRIX_CTRL_CLICK_TRIG);
  lv_obj_add_event_cb(s_ss_show, ss_show_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  r = row(g, true);
  icon(r, LV_SYMBOL_EYE_OPEN, kPower);
  label(r, tr(Str::SsDim));
  s_ss_dim = segmented(r, s_ss_dim_map, index_of(kSsDim, 3, ss.dim), 330, ss_dim_cb);
  s_ss_preview = row(g, true);
  icon(s_ss_preview, LV_SYMBOL_PLAY, kOk);
  label(s_ss_preview, tr(Str::SsPreview));
  make_pressable(s_ss_preview, [](lv_event_t *) { screensaver::start(); });
  for (lv_obj_t *m : {s_ss_wait, s_ss_mode, s_ss_every, s_ss_show, s_ss_dim})
    lv_obj_set_style_opa(m, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_t *ss_hint = make_label(page, font_tiny(), kMuted, tr(Str::SsHint));
  lv_obj_set_width(ss_hint, LV_PCT(100));
  lv_label_set_long_mode(ss_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_hor(ss_hint, 9, 0);
  on_screensaver();

  // HORA Y TIEMPO
  g = section(page, tr(Str::SectionWeather));
  s_place_val = nav_row(g, false, PCD_SYM_PIN, kCpu, tr(Str::Location), [](lv_event_t *) { place::open(); });
  r = row(g, true);
  icon(r, PCD_SYM_THERMO, kWarning);
  label(r, tr(Str::Units));
  s_units = segmented(r, s_units_map, cfg.temp_f ? 1 : 0, 150, units_cb);
  r = row(g, true);
  icon(r, PCD_SYM_CLOCK, kGpu);
  label(r, tr(Str::ClockFormat));
  s_clock = segmented(r, s_clock_map, cfg.clock_12h ? 1 : 0, 150, clock_cb);
  switch_row(g, true, LV_SYMBOL_IMAGE, tr(Str::TodayBg), cfg.today_bg,
             [](lv_event_t *e) { app::set_today_bg(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)); });
  on_place();

  // CRIPTO
  g = section(page, tr(Str::SectionCrypto));
  s_coins_val = nav_row(g, false, PCD_SYM_COINS, kCrypto, tr(Str::CryCoinsTitle), [](lv_event_t *) { coins::open(); });
  r = row(g, true);
  icon(r, LV_SYMBOL_SHUFFLE, kOk);
  label(r, tr(Str::Currency));
  segmented(r, s_cur_map, cfg.crypto_cur < storage::kCurrencyCount ? cfg.crypto_cur : 1, 420, cur_cb);
  r = row(g, true);
  icon(r, LV_SYMBOL_REFRESH, kMuted);
  label(r, tr(Str::CryRefresh));
  segmented(r, s_crefresh_map, cfg.crypto_refresh_s == 15 ? 0 : cfg.crypto_refresh_s == 60 ? 2 : 1, 270, crefresh_cb);
  r = row(g, true);
  icon(r, PCD_SYM_CHART_LINE, kCpu);
  label(r, tr(Str::ChartStyle));
  s_style_map[0] = tr(Str::StyleLine);
  s_style_map[1] = tr(Str::StyleCandles);
  segmented(r, s_style_map, cfg.crypto_style == 1 ? 1 : 0, 225, style_cb);
  on_crypto();

  // PESTAÑAS
  g = section(page, tr(Str::SectionTabs));
  for (int i = 0; i < storage::kTabCount; i++) {
    lv_obj_t *tr_row = row(g, i > 0, 51);
    s_tab_icon[i] = icon(tr_row, "", kCpu);
    s_tab_lbl[i] = label(tr_row, "");
    s_tab_up[i] = arrow_button(tr_row, LV_SYMBOL_UP, -60, i * 2);
    s_tab_down[i] = arrow_button(tr_row, LV_SYMBOL_DOWN, 0, i * 2 + 1);
    s_tab_eye[i] = arrow_button(tr_row, LV_SYMBOL_EYE_OPEN, -132, i, tab_eye_cb);
  }
  refresh_tabs();
  lv_obj_t *tabs_hint = make_label(page, font_tiny(), kMuted, tr(Str::TabsHint));
  lv_obj_set_width(tabs_hint, LV_PCT(100));
  lv_label_set_long_mode(tabs_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_hor(tabs_hint, 9, 0);

  // OPCIONES
  g = section(page, tr(Str::SectionOptions));
  s_alerts = switch_row(g, false, LV_SYMBOL_WARNING, tr(Str::Alerts), cfg.alerts_enabled, alerts_cb);

  // INFORMACIÓN
  g = section(page, tr(Str::SectionInfo));
  const char *keys[7] = {tr(Str::Firmware), "IP",          "Wi-Fi",         tr(Str::DeckBattery),
                         tr(Str::HeapInternal), tr(Str::Psram), tr(Str::Uptime)};
  for (int i = 0; i < 7; i++) {
    lv_obj_t *ir = row(g, i > 0, 36);
    lv_obj_t *k = make_label(ir, font_tiny(), kMuted, keys[i]);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
    s_info[i] = make_label(ir, font_tiny(), kText, "");
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
  for (lv_obj_t *m : {s_ss_wait, s_ss_mode, s_ss_show, s_ss_dim}) set_matrix_enabled(m, ss.enabled);
  set_enabled(s_ss_img_row, ss.enabled);
  set_matrix_enabled(s_ss_every, ss.enabled && ss.bg_mode == static_cast<uint8_t>(storage::SsBg::Cycle));
  set_text(s_ss_img_val, ui::wallpaper::name(ss.bg));
  // Auto sleep is replaced while the screensaver is on.
  set_matrix_enabled(s_sleep_m, !ss.enabled);
  char b[96];
  if (ss.enabled)
    snprintf(b, sizeof(b), "%s  ·  %s", tr(Str::AutoSleep), tr(Str::SsReplacedBySaver));
  else
    snprintf(b, sizeof(b), "%s", tr(Str::AutoSleep));
  set_text(s_sleep_lbl, b);
  set_color(s_sleep_lbl, ss.enabled ? kMuted : kText);
}

void on_crypto() {
  if (!s_coins_val) return;
  int8_t idx[crypto::kMaxCoins];
  const int n = crypto::parse_watch(app::state().settings.crypto_coins, idx, crypto::kMaxCoins);
  char buf[64];
  char list[40];
  crypto::format_watch(idx, n < 4 ? n : 4, list, sizeof(list));
  for (char *c = list; *c; c++)
    if (*c == ',') *c = ' ';
  if (n == 0)
    snprintf(buf, sizeof(buf), "%s", tr(Str::LocationNone));
  else if (n <= 4)
    snprintf(buf, sizeof(buf), "%s", list);
  else
    snprintf(buf, sizeof(buf), "%s +%d", list, n - 4);
  set_text(s_coins_val, buf);
}

void on_place() {
  if (!s_place_val) return;
  const auto &cfg = app::state().settings;
  set_text(s_place_val, cfg.place_name[0] ? cfg.place_name : tr(Str::LocationNone));
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
  if (bat.source == battery::Source::Usb)
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
