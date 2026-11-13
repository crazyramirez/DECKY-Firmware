// Setup assistant: opens at power-on when no Wi-Fi network is stored (and from Settings).
//   left:  title, "Step n of 5", the steps (number or check, current one highlighted, tap to jump)
//   right: the current step — icon, title, what to do, live status, its action, Next / Skip
// Steps: language · Wi-Fi · pair PC · city · done. Wi-Fi and pairing reuse their pages: the
// sheet steps aside (a floating "Setup" chip brings it back) and returns by itself as soon as the
// step is done (Wi-Fi connected, PC paired and live). City search opens on top. The Crypto tab
// starts with the default coins (Settings > Crypto > Coins to change them): no step for it.

#include <stdio.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "network/net_task.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace setup {
namespace {

using namespace theme;

enum Step : int { kLang = 0, kWifi, kPair, kCity, kDone, kSteps };

lv_obj_t *s_sheet = nullptr;
lv_obj_t *s_chip = nullptr;
lv_obj_t *s_counter = nullptr;
lv_obj_t *s_row[kSteps] = {};
lv_obj_t *s_dot[kSteps] = {};
lv_obj_t *s_dot_lbl[kSteps] = {};
lv_obj_t *s_icon = nullptr;
lv_obj_t *s_head = nullptr;
lv_obj_t *s_desc = nullptr;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_action = nullptr;
lv_obj_t *s_action_lbl = nullptr;
lv_obj_t *s_lang = nullptr;
lv_obj_t *s_next = nullptr;
lv_obj_t *s_next_lbl = nullptr;
int s_step = kLang;
bool s_active = false;
bool s_away = false;  // the user is on the Wi-Fi / Pair page for the current step
const char *s_lang_map[] = {"Español", "English", ""};

const char *step_name(int s) {
  switch (s) {
    case kLang: return tr(Str::Language);
    case kWifi: return tr(Str::WifiTitle);
    case kPair: return tr(Str::PairTitle);
    case kCity: return tr(Str::Location);
    default: return tr(Str::CryDone);
  }
}

const char *step_icon(int s) {
  switch (s) {
    case kLang: return LV_SYMBOL_KEYBOARD;
    case kWifi: return LV_SYMBOL_WIFI;
    case kPair: return LV_SYMBOL_SHUFFLE;
    case kCity: return PCD_SYM_PIN;
    default: return LV_SYMBOL_OK;
  }
}

bool pc_live() {
  const net::Status ns = net::status();
  return ns.paired && app::state().link == app::LinkState::Online;
}

bool step_done(int s) {
  const auto &cfg = app::state().settings;
  switch (s) {
    case kLang: return true;
    case kWifi: return net::status().wifi_connected;
    case kPair: return pc_live();
    case kCity: return cfg.place_name[0] != '\0';
    default: return true;
  }
}

void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on)
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void build();
void render();

void close_all() {
  if (s_sheet) lv_obj_del(s_sheet);
  if (s_chip) lv_obj_del(s_chip);
  s_sheet = s_chip = nullptr;
  for (auto &o : s_row) o = nullptr;
  s_active = false;
  s_away = false;
}

// Step aside to a page (Wi-Fi / Pair) with a chip to come back.
void go_away(Page p) {
  s_away = true;
  show(s_sheet, false);
  show(s_chip, true);
  lv_obj_move_foreground(s_chip);
  navigate(p);
}

void come_back(bool advance) {
  s_away = false;
  if (advance && s_step < kDone) s_step++;
  show(s_chip, false);
  show(s_sheet, true);
  lv_obj_move_foreground(s_sheet);
  render();
}

void action_cb(lv_event_t *) {
  switch (s_step) {
    case kWifi: go_away(Page::Wifi); break;
    case kPair: go_away(Page::Pair); break;
    case kCity: place::open(); break;  // sheet on top of this one
    case kDone: {
      close_all();
      // Start where the user will live: the first visible tab.
      const auto &cfg = app::state().settings;
      Page first = Page::Settings;
      for (int i = storage::kTabCount - 1; i >= 0; i--)
        if (!(cfg.tab_hidden & (1u << cfg.tab_order[i]))) first = tab_page(cfg.tab_order[i]);
      navigate(first);
      break;
    }
    default: break;
  }
}

void next_cb(lv_event_t *) {
  if (s_step < kDone) s_step++;
  render();
}

void row_cb(lv_event_t *e) {
  s_step = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  render();
}

void lang_cb(lv_event_t *e) {
  const uint16_t sel = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  app::set_language(sel == 1 ? storage::Lang::En : storage::Lang::Es);  // UI rebuild -> on_rebuild()
}

lv_obj_t *pill_button(lv_obj_t *parent, uint32_t bg, uint32_t fg, lv_obj_t **lbl, lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_style_radius(b, 20, 0);
  lv_obj_set_style_bg_color(b, c(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_mix(c(bg), c(kBg), 200), LV_STATE_PRESSED);
  lv_obj_t *l = make_label(b, font_small(), fg, "");
  lv_obj_center(l);
  if (lbl) *lbl = l;
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

void build() {
  s_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_sheet);
  lv_obj_set_size(s_sheet, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_sheet, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE);

  // Left: title, counter, steps, exit.
  lv_obj_t *t = make_label(s_sheet, font_small(), kText, tr(Str::SetupTitle));
  lv_obj_set_size(t, 168, lv_font_get_line_height(font_small()));
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(t, 12, 8);
  s_counter = make_label(s_sheet, font_micro(), kMuted, "");
  lv_obj_set_pos(s_counter, 12, 30);
  for (int i = 0; i < kSteps; i++) {
    lv_obj_t *r = lv_obj_create(s_sheet);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 166, 28);
    lv_obj_set_pos(r, 6, 52 + i * 30);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, c(kSurfaceHi), 0);
    lv_obj_set_style_pad_left(r, 6, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(r, row_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_t *d = lv_obj_create(r);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 18, 18);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 1, 0);
    lv_obj_align(d, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
    s_dot_lbl[i] = make_label(d, font_tiny(), kText, "");
    lv_obj_center(s_dot_lbl[i]);
    lv_obj_t *l = make_label(r, font_tiny(), kText, step_name(i));
    lv_obj_set_size(l, 132, lv_font_get_line_height(font_tiny()));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 26, 0);
    s_row[i] = r;
    s_dot[i] = d;
  }
  lv_obj_t *exit = lv_btn_create(s_sheet);
  lv_obj_remove_style_all(exit);
  lv_obj_set_size(exit, 166, 22);
  lv_obj_set_pos(exit, 6, 246);
  lv_obj_t *el = make_label(exit, font_micro(), kMuted, tr(Str::SetupExit));
  lv_obj_align(el, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_add_event_cb(exit, [](lv_event_t *) { close_all(); }, LV_EVENT_CLICKED, nullptr);

  // Right: the current step.
  lv_obj_t *card = make_card(s_sheet);
  lv_obj_set_pos(card, 182, 8);
  lv_obj_set_size(card, kScreenW - 182 - 8, kScreenH - 16);
  lv_obj_set_style_pad_all(card, 14, 0);
  const int inner = kScreenW - 182 - 8 - 28;  // 262
  s_icon = make_label(card, font_title(), kCpu, "");
  lv_obj_set_pos(s_icon, 0, -2);
  s_head = make_label(card, font_body(), kText, "");
  lv_obj_set_size(s_head, inner - 34, lv_font_get_line_height(font_body()));
  lv_label_set_long_mode(s_head, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_head, 34, 0);
  s_desc = make_label(card, font_tiny(), kMuted, "");
  lv_obj_set_width(s_desc, inner);
  lv_label_set_long_mode(s_desc, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(s_desc, 0, 34);
  s_status = make_label(card, font_tiny(), kOk, "");
  lv_obj_set_size(s_status, inner, lv_font_get_line_height(font_tiny()));
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_status, 0, 112);
  s_action = pill_button(card, kCpu, kBg, &s_action_lbl, action_cb);
  lv_obj_set_size(s_action, inner, 34);
  lv_obj_set_pos(s_action, 0, 136);
  s_lang = lv_btnmatrix_create(card);
  lv_btnmatrix_set_map(s_lang, s_lang_map);
  lv_btnmatrix_set_btn_ctrl_all(s_lang, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(s_lang, true);
  lv_btnmatrix_set_btn_ctrl(s_lang, app::state().settings.lang == storage::Lang::En ? 1 : 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(s_lang, inner, 34);
  lv_obj_set_pos(s_lang, 0, 136);
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
  s_next = pill_button(card, kSurfaceHi, kText, &s_next_lbl, next_cb);
  lv_obj_set_size(s_next, 110, 30);
  lv_obj_align(s_next, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_border_color(s_next, c(kBorder), 0);
  lv_obj_set_style_border_width(s_next, 1, 0);

  // Floating chip shown while the user is on the Wi-Fi / Pair page for this assistant.
  s_chip = lv_btn_create(lv_layer_top());
  lv_obj_remove_style_all(s_chip);
  lv_obj_set_size(s_chip, LV_SIZE_CONTENT, 26);
  lv_obj_set_style_pad_hor(s_chip, 10, 0);
  lv_obj_set_style_radius(s_chip, 13, 0);
  lv_obj_set_style_bg_color(s_chip, c(kCpu), 0);
  lv_obj_set_style_bg_opa(s_chip, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(s_chip, 12, 0);
  lv_obj_set_style_shadow_opa(s_chip, LV_OPA_50, 0);
  lv_obj_align(s_chip, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  char b[48];
  snprintf(b, sizeof(b), LV_SYMBOL_LEFT " %s", tr(Str::SetupChip));
  lv_obj_t *cl = make_label(s_chip, font_tiny(), kBg, b);
  lv_obj_center(cl);
  lv_obj_add_event_cb(s_chip, [](lv_event_t *) { come_back(false); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(s_chip, LV_OBJ_FLAG_HIDDEN);
}

void render() {
  if (!s_sheet) return;
  char b[160];
  snprintf(b, sizeof(b), tr(Str::SetupStepFmt), s_step + 1, static_cast<int>(kSteps));
  set_text(s_counter, b);
  for (int i = 0; i < kSteps; i++) {
    const bool cur = i == s_step;
    const bool done = i < kDone && i != kLang && step_done(i);
    lv_obj_set_style_bg_opa(s_row[i], cur ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_dot[i], c(done ? kOk : cur ? kCpu : kSurface), 0);
    lv_obj_set_style_border_color(s_dot[i], c(done ? kOk : cur ? kCpu : kBorder), 0);
    if (done) {
      set_text(s_dot_lbl[i], LV_SYMBOL_OK);
      lv_obj_set_style_text_font(s_dot_lbl[i], font_tiny(), 0);  // 12 px carries the check icon
      set_color(s_dot_lbl[i], kBg);
    } else {
      snprintf(b, sizeof(b), "%d", i + 1);
      set_text(s_dot_lbl[i], b);
      lv_obj_set_style_text_font(s_dot_lbl[i], font_micro(), 0);
      set_color(s_dot_lbl[i], cur ? kBg : kMuted);
    }
  }
  set_text(s_icon, step_icon(s_step));
  set_color(s_icon, s_step == kDone ? kOk : kCpu);
  set_text(s_head, s_step == kDone ? tr(Str::SetupDoneTitle) : step_name(s_step));

  const net::Status ns = net::status();
  const auto &cfg = app::state().settings;
  const char *desc = "";
  const char *action = nullptr;
  bool enabled = true;
  b[0] = '\0';
  uint32_t status_color = kOk;
  switch (s_step) {
    case kLang: desc = tr(Str::SetupLangDesc); break;
    case kWifi:
      desc = tr(Str::SetupWifiDesc);
      action = tr(Str::SetupWifiBtn);
      if (ns.wifi_connected) snprintf(b, sizeof(b), LV_SYMBOL_OK " %s · %s", tr(Str::ConnectedTo), ns.ip);
      break;
    case kPair:
      desc = tr(Str::SetupPairDesc);
      action = tr(Str::PairTitle);
      if (pc_live()) {
        snprintf(b, sizeof(b), LV_SYMBOL_OK " %s %s", tr(Str::PairPairedWith), ns.agent_name);
      } else if (!ns.wifi_connected) {
        snprintf(b, sizeof(b), "%s", tr(Str::SetupNeedWifi));
        status_color = kWarning;
        enabled = false;
      }
      break;
    case kCity:
      desc = tr(Str::SetupCityDesc);
      action = tr(Str::ChoosePlace);
      if (cfg.place_name[0]) {
        snprintf(b, sizeof(b), LV_SYMBOL_OK " %s", cfg.place_name);
      } else if (!ns.wifi_connected) {
        snprintf(b, sizeof(b), "%s", tr(Str::SetupNeedWifi));
        status_color = kWarning;
        enabled = false;
      }
      break;
    default:
      desc = tr(Str::SetupDoneDesc);
      action = tr(Str::SetupStart);
      break;
  }
  set_text(s_desc, desc);
  set_text(s_status, b);
  set_color(s_status, status_color);
  show(s_lang, s_step == kLang);
  show(s_action, action != nullptr);
  if (action) set_text(s_action_lbl, action);
  if (enabled)
    lv_obj_clear_state(s_action, LV_STATE_DISABLED);
  else
    lv_obj_add_state(s_action, LV_STATE_DISABLED);
  lv_obj_set_style_bg_opa(s_action, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
  show(s_next, s_step != kDone);
  set_text(s_next_lbl, step_done(s_step) ? tr(Str::SetupNext) : tr(Str::SetupSkip));
}

}  // namespace

void open() {
  close_all();
  s_step = net::status().wifi_connected ? kPair : kLang;
  if (s_step == kPair && pc_live()) s_step = kCity;
  s_active = true;
  build();
  lv_obj_move_foreground(s_sheet);
  render();
}

bool active() { return s_active; }

void on_rebuild() {
  if (!s_active) return;
  const int step = s_step;
  const bool away = s_away;
  close_all();
  s_active = true;
  s_step = step;
  build();
  if (away) {
    s_away = true;
    show(s_sheet, false);
    show(s_chip, true);
  }
  render();
}

void tick() {
  if (!s_active) return;
  if (s_away) {
    // Back as soon as the step the user went to do is done.
    if (step_done(s_step)) come_back(true);
    return;
  }
  render();  // live status (Wi-Fi connecting, PC coming online, place / coins picked on top)
}

}  // namespace setup
}  // namespace ui
