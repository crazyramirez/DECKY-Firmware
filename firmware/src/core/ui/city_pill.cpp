// Today's city switcher, shared by every deck (ui/cities_ui.h): the pill and the swipe.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/cities.h"
#include "app/state_store.h"
#include "ui/cities_ui.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace cities {
namespace {

using namespace theme;

int text_w(const char *s, const lv_font_t *f) {
  lv_point_t sz;
  lv_txt_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x;
}

void show(lv_obj_t *o, bool on) {
  if (on && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (!on && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

// One dot per city; the shown one is a longer accent capsule.
void build_dots(Pill &p, int n, int cur) {
  lv_obj_clean(p.dots);
  for (int i = 0; i < n; i++) {
    lv_obj_t *d = plain(p.dots);
    lv_obj_set_size(d, i == cur ? p.dot * 9 / 4 : p.dot, p.dot);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, c(i == cur ? kCpu : kText), 0);
    lv_obj_set_style_bg_opa(d, i == cur ? LV_OPA_COVER : LV_OPA_40, 0);
  }
  p.dots_n = n;
  p.dots_cur = cur;
}

int dots_w(const Pill &p, int n) { return (n - 1) * p.dot + p.dot * 9 / 4 + (n - 1) * (p.dot * 2 / 3); }

void swipe_cb(lv_event_t *) {
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev) return;
  const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if ((dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) || app::city_count() < 2) return;
  lv_indev_wait_release(indev);  // the finger lifting must not also tap what is under it
  app::step_city(dir == LV_DIR_LEFT ? 1 : -1);
}

}  // namespace

void fmt_offset(char *buf, size_t n, int minutes) {
  if (minutes == 0) {
    snprintf(buf, n, "%s", tr(Str::CitySameTime));
    return;
  }
  const int a = abs(minutes);
  if (a % 60 == 0)
    snprintf(buf, n, "%c%d h", minutes < 0 ? '-' : '+', a / 60);
  else
    snprintf(buf, n, "%c%d:%02d h", minutes < 0 ? '-' : '+', a / 60, a % 60);
}

void fit_text(char *out, size_t n, const char *s, const lv_font_t *font, int max_w) {
  if (!n) return;
  if (text_w(s, font) <= max_w) {
    snprintf(out, n, "%s", s);
    return;
  }
  static const char kDots[] = "\xE2\x80\xA6";  // U+2026, the ellipsis
  char tmp[96];
  uint32_t keep = 0, i = 0;
  while (s[i]) {
    uint32_t next = i;
    _lv_txt_encoded_next(s, &next);
    if (next + sizeof(kDots) > sizeof(tmp) || next + sizeof(kDots) > n) break;
    memcpy(tmp, s, next);
    memcpy(tmp + next, kDots, sizeof(kDots));
    if (text_w(tmp, font) > max_w) break;
    keep = i = next;
  }
  while (keep && s[keep - 1] == ' ') keep--;
  if (keep + sizeof(kDots) > n) keep = 0;
  memcpy(out, s, keep);
  if (keep + sizeof(kDots) <= n)
    memcpy(out + keep, kDots, sizeof(kDots));
  else
    out[0] = '\0';
}

void fmt_time(char *buf, size_t n, const struct tm &t) {
  if (app::state().settings.clock_12h)
    snprintf(buf, n, "%d:%02d %s", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  else
    snprintf(buf, n, "%02d:%02d", t.tm_hour, t.tm_min);
}

void pill_create(Pill &p, lv_obj_t *parent, const lv_font_t *font, bool chip, int width, int dot) {
  p = Pill{};
  p.font = font;
  p.dot = dot;
  const int lh = lv_font_get_line_height(font);
  p.root = lv_obj_create(parent);
  lv_obj_remove_style_all(p.root);
  lv_obj_clear_flag(p.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(p.root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(p.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(p.root, lh / 3, 0);
  if (chip) {
    p.max_w = width;
    p.pad = lh * 3 / 4;
    lv_obj_set_size(p.root, LV_SIZE_CONTENT, lh + 2 * (lh * 2 / 5));
    lv_obj_set_style_pad_hor(p.root, p.pad, 0);
    lv_obj_set_style_radius(p.root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p.root, c(kSurface), 0);
    lv_obj_set_style_bg_opa(p.root, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(p.root, c(kSurfaceHi), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(p.root, LV_OPA_COVER, LV_STATE_PRESSED);
  } else {
    p.row_w = width;
    lv_obj_set_size(p.root, width, lh);
    lv_obj_set_style_opa(p.root, LV_OPA_70, LV_STATE_PRESSED);
  }
  lv_obj_set_ext_click_area(p.root, lh / 2);
  p.pin = make_label(p.root, font, kCpu, PCD_SYM_PIN);
  p.name = make_label(p.root, font, kText, "");
  lv_label_set_long_mode(p.name, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(p.name, lh, lh);
  p.off = make_label(p.root, font, kMuted, "");
  if (!chip) {  // pushes the dots to the right edge
    p.spacer = plain(p.root);
    lv_obj_set_height(p.spacer, 1);
    lv_obj_set_flex_grow(p.spacer, 1);
  }
  p.dots = plain(p.root);
  lv_obj_set_size(p.dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(p.dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(p.dots, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(p.dots, dot * 2 / 3, 0);
  if (chip) lv_obj_set_style_pad_left(p.dots, lh / 3, 0);  // the dots stand a little apart from the text
  p.plus = make_label(p.root, font, kCpu, LV_SYMBOL_PLUS);
  lv_obj_add_event_cb(p.root, [](lv_event_t *) { open(); }, LV_EVENT_CLICKED, nullptr);
  pill_update(p);
}

void pill_update(Pill &p) {
  if (!p.root) return;
  const int n = app::city_count(), cur = app::today_city();
  show(p.root, n > 0);
  if (n == 0) return;
  const int lh = lv_font_get_line_height(p.font), gap = lh / 3;
  // How far ahead that city's clock is (nothing for the main place, whose time the deck shows anyway).
  char off[40] = "";
  int minutes = 0;
  if (p.offset && cur > 0 && app::city_offset(cur, minutes)) {
    char o[28];
    fmt_offset(o, sizeof(o), minutes);
    snprintf(off, sizeof(off), "·  %s", o);
  }
  set_text(p.off, off);
  show(p.off, off[0] != '\0');
  show(p.dots, n > 1);
  show(p.plus, n == 1);
  if (n > 1 && (n != p.dots_n || cur != p.dots_cur)) build_dots(p, n, cur);
  // The name takes what the rest leaves, with "..." when it does not fit.
  const int trail = n > 1 ? dots_w(p, n) + (p.spacer ? 0 : gap) : text_w(LV_SYMBOL_PLUS, p.font);
  int items = 3 + (off[0] ? 1 : 0) + (p.spacer ? 1 : 0);  // pin, name, trail (+ offset, spacer)
  const int total = p.row_w > 0 ? p.row_w : p.max_w - 2 * p.pad;
  const int rest = text_w(PCD_SYM_PIN, p.font) + (off[0] ? text_w(off, p.font) : 0) + trail + (items - 1) * gap;
  char name[64];
  fit_text(name, sizeof(name), app::city_name(cur), p.font, total - rest > lh ? total - rest : lh);
  lv_obj_set_width(p.name, text_w(name, p.font) + 2);
  set_text(p.name, name);
}

void attach_swipe(lv_obj_t *page) {
  // LVGL only calls it a gesture while every read of the touch panel moves a few pixels, and starts
  // counting again at the first read that does not: a touch controller slower than the read period, or
  // an unhurried finger, never gets there. Without the speed test a gesture is simply 50 px (the
  // default limit) from where the finger went down, on the axis that moved most. Nothing else in the
  // UI uses gestures, and a page that scrolls keeps its drag (no gesture while something scrolls).
  for (lv_indev_t *in = lv_indev_get_next(nullptr); in; in = lv_indev_get_next(in))
    if (in->driver && in->driver->type == LV_INDEV_TYPE_POINTER) in->driver->gesture_min_velocity = 0;
  lv_obj_clear_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);  // gestures from anything inside stop here
  lv_obj_add_event_cb(page, swipe_cb, LV_EVENT_GESTURE, nullptr);
}

}  // namespace cities
}  // namespace ui
