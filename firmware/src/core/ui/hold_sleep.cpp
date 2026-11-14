#include "ui/hold_sleep.h"

#include <stdlib.h>

#include "app/power.h"
#include "deck_config.h"
#include "ui/i18n.h"
#include "ui/ui_internal.h"

namespace ui {
namespace hold_sleep {
namespace {

using namespace theme;

constexpr uint32_t kShowMs = 350;    // a tap never shows the ring
constexpr uint32_t kSleepMs = 2000;  // finger down this long: sleep

lv_obj_t *s_root = nullptr;  // the screensaver's root
lv_obj_t *s_veil = nullptr;  // full-screen dimming, holds the ring (created on the first long hold)
lv_obj_t *s_arc = nullptr, *s_icon = nullptr;
uint32_t s_t0 = 0;
lv_point_t s_p0 = {0, 0};
bool s_armed = false, s_fired = false;
int s_shown = -1;

lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);  // the touch stays on the object being held
  return o;
}

void build() {
  // Sizes from the deck's type scale: the power symbol in the value font, the ring ~3 lines tall.
  const lv_font_t *f_icon = font_value(), *f_text = font_small();
  const int lh = lv_font_get_line_height(f_icon);
  const int d = lh * 3, w = d / 16 > 4 ? d / 16 : 4;

  s_veil = plain(s_root);
  lv_obj_set_size(s_veil, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_veil, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_veil, LV_OPA_TRANSP, 0);

  // A dark rounded panel holds the ring and its label, so nothing of the picture shows through them.
  lv_obj_t *panel = plain(s_veil);
  lv_obj_set_size(panel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x0B0F17), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
  lv_obj_set_style_radius(panel, d / 4, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_white(), 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_10, 0);
  lv_obj_set_style_pad_hor(panel, d / 3, 0);
  lv_obj_set_style_pad_top(panel, d / 4, 0);
  lv_obj_set_style_pad_bottom(panel, d / 5, 0);
  lv_obj_set_style_pad_row(panel, d / 8, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  s_arc = lv_arc_create(panel);
  lv_obj_remove_style_all(s_arc);
  lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_arc, d, d);
  lv_arc_set_bg_angles(s_arc, 0, 360);
  lv_arc_set_rotation(s_arc, 270);  // fills clockwise from the top
  lv_arc_set_range(s_arc, 0, 100);
  lv_arc_set_value(s_arc, 0);
  lv_obj_set_style_arc_width(s_arc, w, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_arc, lv_color_hex(kText), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s_arc, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, w, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_arc, lv_color_hex(kText), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

  s_icon = make_label(s_arc, f_icon, kText, LV_SYMBOL_POWER);
  lv_obj_center(s_icon);

  lv_obj_t *t = make_label(panel, f_text, kText, tr(Str::DeepSleepOk));  // "Sleep"
  lv_obj_set_style_text_opa(t, LV_OPA_80, 0);
}

void show(int pct) {
  if (!s_root) return;
  if (pct < 0) {
    if (s_veil && !lv_obj_has_flag(s_veil, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(s_veil, LV_OBJ_FLAG_HIDDEN);
    s_shown = -1;
    return;
  }
  if (pct > 100) pct = 100;
  if (!s_veil) build();
  if (lv_obj_has_flag(s_veil, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(s_veil, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_veil);
  if (pct == s_shown) return;
  s_shown = pct;
  // The picture fades towards black (to 80 %) as the ring fills; the symbol brightens.
  lv_obj_set_style_bg_opa(s_veil, static_cast<lv_opa_t>(LV_OPA_50 + pct * (LV_OPA_80 - LV_OPA_50) / 100), 0);
  lv_obj_set_style_text_opa(s_icon, static_cast<lv_opa_t>(LV_OPA_60 + pct * (LV_OPA_COVER - LV_OPA_60) / 100), 0);
  lv_arc_set_value(s_arc, static_cast<int16_t>(pct));
}

void sleep_now(void *) {
  s_armed = false;
#if PCD_HAS_DEEP_SLEEP
  power::deep_sleep();  // closes the screensaver, waits for the finger to lift
#else
  power::screen_off();  // closes the screensaver; the finger still down does not wake it
#endif
}

void cancel() {
  s_armed = false;
  show(-1);
}

void event_cb(lv_event_t *e) {
  if (!s_root || s_fired) return;
  lv_indev_t *indev = lv_indev_get_act();
  lv_point_t p = s_p0;
  if (indev) lv_indev_get_point(indev, &p);
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      s_t0 = lv_tick_get();
      s_p0 = p;
      s_armed = true;
      break;
    case LV_EVENT_PRESSING: {
      if (!s_armed) break;
      // Sliding the finger away cancels (it is a swipe, not a hold): 6 % of the shorter side.
      lv_coord_t side = LV_MIN(lv_obj_get_width(s_root), lv_obj_get_height(s_root));
      const int tol = side * 6 / 100, dx = p.x - s_p0.x, dy = p.y - s_p0.y;
      if (dx * dx + dy * dy > tol * tol) {
        cancel();
        break;
      }
      const uint32_t held = lv_tick_elaps(s_t0);
      if (held >= kSleepMs) {
        show(100);
        s_fired = true;
        lv_indev_wait_release(indev);  // lifting the finger is not a tap (no card opens)
        lv_async_call(sleep_now, nullptr);  // out of the event: sleeping closes the screensaver
      } else if (held >= kShowMs) {
        show(static_cast<int>((held - kShowMs) * 100 / (kSleepMs - kShowMs)));
      }
      break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      cancel();
      break;
    default:
      break;
  }
}

}  // namespace

void begin(lv_obj_t *root) {
  s_root = root;
  s_veil = s_arc = s_icon = nullptr;
  s_armed = s_fired = false;
  s_shown = -1;
}

void attach(lv_obj_t *o) {
  lv_obj_add_event_cb(o, event_cb, LV_EVENT_ALL, nullptr);
}

void end() {
  s_root = s_veil = s_arc = s_icon = nullptr;
  s_armed = false;
  s_shown = -1;
}

void preview(int pct) { show(pct); }

}  // namespace hold_sleep
}  // namespace ui
