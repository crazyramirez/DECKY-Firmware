#include "ui/battery_badge.h"

#include <stdio.h>

#include "app/state_store.h"
#include "board/battery.h"
#include "ui/ui_internal.h"

namespace ui {
namespace {

using namespace theme;

lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);  // a touch goes through to the screensaver (closes it)
  return o;
}

// White while there is charge to spare, amber at 20 % and under, red at 10 % and under.
int s_dev_pct = -1;

uint32_t charge_color(int pct) { return pct <= 10 ? kCritical : pct <= 20 ? kWarning : kText; }

}  // namespace

void battery_badge_create(BatteryBadge &b, lv_obj_t *parent, const lv_font_t *font, int body_w, int body_h) {
  b = BatteryBadge{};
  const int line = body_h >= 16 ? 2 : 1;           // outline
  const int gap = body_h >= 16 ? 2 : 1;            // between the outline and the fill
  const int cap_w = body_h >= 16 ? 3 : 2, cap_h = body_h / 2;
  b.root = plain(parent);
  lv_obj_set_size(b.root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(b.root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b.root, body_h / 2, 0);
  b.pct = make_label(b.root, font, kText, "");
  lv_obj_set_style_text_opa(b.pct, LV_OPA_90, 0);
  // Battery: outline + cap in one row (no gap), the fill inside the outline.
  lv_obj_t *icon = plain(b.root);
  lv_obj_set_size(icon, body_w + cap_w, body_h);
  b.body = plain(icon);
  lv_obj_set_size(b.body, body_w, body_h);
  lv_obj_set_style_radius(b.body, body_h / 4 + 1, 0);
  lv_obj_set_style_border_width(b.body, line, 0);
  lv_obj_set_style_border_color(b.body, c(kText), 0);
  lv_obj_set_style_border_opa(b.body, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(b.body, line + gap, 0);
  b.cap = plain(icon);
  lv_obj_set_size(b.cap, cap_w, cap_h);
  lv_obj_set_pos(b.cap, body_w, (body_h - cap_h) / 2);
  lv_obj_set_style_radius(b.cap, 1, 0);
  lv_obj_set_style_bg_color(b.cap, c(kText), 0);
  lv_obj_set_style_bg_opa(b.cap, LV_OPA_80, 0);
  b.fill = plain(b.body);
  b.inner_w = body_w - 2 * (line + gap);
  lv_obj_set_size(b.fill, b.inner_w, body_h - 2 * (line + gap));
  lv_obj_set_style_radius(b.fill, body_h >= 16 ? 2 : 1, 0);
  lv_obj_set_style_bg_opa(b.fill, LV_OPA_COVER, 0);
  lv_obj_add_flag(b.root, LV_OBJ_FLAG_HIDDEN);
  battery_badge_update(b);
}

void battery_badge_dev(int pct) { s_dev_pct = pct; }

void battery_badge_update(BatteryBadge &b) {
  const int pct_override = s_dev_pct;
  if (!b.root) return;
  const uint8_t mode = app::state().settings.ss.battery;
  const battery::State st = battery::state();
  int pct = pct_override >= 0 ? pct_override : st.source == battery::Source::Battery ? st.pct : -1;
  if (pct > 100) pct = 100;
  // Only on battery (a level to show), never on USB; and only if the user wants it.
  const bool on = mode != 0 && pct >= 0 && (battery::sensed() || pct_override >= 0);
  if (!on) {
    if (!lv_obj_has_flag(b.root, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(b.root, LV_OBJ_FLAG_HIDDEN);
    b.shown_pct = -2;
    return;
  }
  if (lv_obj_has_flag(b.root, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(b.root, LV_OBJ_FLAG_HIDDEN);
  if (pct == b.shown_pct && static_cast<int8_t>(mode) == b.shown_mode) return;
  b.shown_pct = static_cast<int8_t>(pct);
  b.shown_mode = static_cast<int8_t>(mode);
  const uint32_t col = charge_color(pct);
  if (mode == 2) {
    char t[8];
    snprintf(t, sizeof(t), "%d %%", pct);
    set_text(b.pct, t);
    set_color(b.pct, col);
    if (lv_obj_has_flag(b.pct, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(b.pct, LV_OBJ_FLAG_HIDDEN);
  } else if (!lv_obj_has_flag(b.pct, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(b.pct, LV_OBJ_FLAG_HIDDEN);
  }
  // Never an empty-looking battery while there is charge left: at least a sliver.
  int w = (b.inner_w * pct + 50) / 100;
  if (pct > 0 && w < 2) w = 2;
  lv_obj_set_width(b.fill, w);
  if (w == 0)
    lv_obj_add_flag(b.fill, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_clear_flag(b.fill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(b.fill, c(col), 0);
  lv_obj_set_style_border_color(b.body, c(pct <= 10 ? kCritical : kText), 0);
}

}  // namespace ui
