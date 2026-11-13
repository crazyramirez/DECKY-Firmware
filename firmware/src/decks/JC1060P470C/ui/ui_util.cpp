#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "telemetry/history.h"
#include "ui/ui_internal.h"

namespace ui {

void grad_bg_create(GradBg &g, lv_obj_t *parent, bool horizontal, int w, int h, int pad_x, int pad_y) {
  g.horizontal = horizontal;
  g.len = horizontal ? w : h;
  const int sw = horizontal ? w : 4, sh = horizontal ? 4 : h;
  if (!g.px) g.px = static_cast<uint16_t *>(heap_caps_malloc(sw * sh * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g.dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  g.dsc.header.w = sw;
  g.dsc.header.h = sh;
  g.dsc.data_size = sw * sh * sizeof(lv_color_t);
  g.dsc.data = reinterpret_cast<const uint8_t *>(g.px);
  g.img = lv_img_create(parent);
  lv_obj_add_flag(g.img, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(g.img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(g.img, -pad_x, -pad_y);
  lv_obj_move_to_index(g.img, 0);
  if (g.px) {
    for (int i = 0; i < sw * sh; i++) g.px[i] = 0;
    lv_img_set_src(g.img, &g.dsc);
  }
  lv_obj_set_size(g.img, w, h);  // larger than the strip: LVGL tiles it
}

void grad_bg_set(GradBg &g, lv_color_t from, lv_color_t to, uint8_t stop) {
  if (!g.px || g.len <= 0) return;
  static const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const lv_color32_t a{.full = lv_color_to32(from)}, b{.full = lv_color_to32(to)};
  const int end = g.len * stop / 255 > 0 ? g.len * stop / 255 : 1;
  const int sw = g.dsc.header.w;
  for (int i = 0; i < g.len; i++) {
    const float t = i >= end ? 1.0f : static_cast<float>(i) / end;
    const float r = a.ch.red + (b.ch.red - a.ch.red) * t;
    const float gr = a.ch.green + (b.ch.green - a.ch.green) * t;
    const float bl = a.ch.blue + (b.ch.blue - a.ch.blue) * t;
    for (int k = 0; k < 4; k++) {
      const int x = g.horizontal ? i : k, y = g.horizontal ? k : i;
      const float th = (kBayer[y & 3][x & 3] + 0.5f) / 16.0f;
      int r5 = static_cast<int>(r * (31.0f / 255.0f) + th), g6 = static_cast<int>(gr * (63.0f / 255.0f) + th);
      int b5 = static_cast<int>(bl * (31.0f / 255.0f) + th);
      r5 = r5 > 31 ? 31 : r5;
      g6 = g6 > 63 ? 63 : g6;
      b5 = b5 > 31 ? 31 : b5;
      lv_color_t c;
      c.full = 0;
      LV_COLOR_SET_R(c, r5);
      LV_COLOR_SET_G(c, g6);
      LV_COLOR_SET_B(c, b5);
      g.px[y * sw + x] = c.full;
    }
  }
  lv_obj_invalidate(g.img);
}

lv_obj_t *make_page(lv_obj_t *parent, bool scrollable) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_hor(p, theme::kMargin, 0);
  lv_obj_set_style_pad_ver(p, 15, 0);
  lv_obj_set_style_pad_row(p, theme::kGap, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  if (scrollable) {
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_ACTIVE);
  } else {
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  }
  return p;
}

lv_obj_t *make_chart(lv_obj_t *parent, int points, int y_max) {
  lv_obj_t *ch = lv_chart_create(parent);
  theme::style_chart(ch);
  lv_chart_set_point_count(ch, points);
  lv_chart_set_range(ch, LV_CHART_AXIS_PRIMARY_Y, 0, y_max);
  return ch;
}

void set_text(lv_obj_t *label, const char *text) {
  if (!label) return;
  const char *cur = lv_label_get_text(label);
  if (cur && strcmp(cur, text) == 0) return;
  lv_label_set_text(label, text);
}

void set_color(lv_obj_t *obj, uint32_t rgb) {
  if (!obj) return;
  const lv_color_t want = lv_color_hex(rgb);
  if (lv_obj_get_style_text_color(obj, LV_PART_MAIN).full == want.full) return;
  lv_obj_set_style_text_color(obj, want, 0);
}

void set_bar(lv_obj_t *bar, float value, float max) {
  if (!bar) return;
  int32_t pct = 0;
  if (telemetry::has(value) && max > 0) {
    pct = static_cast<int32_t>(lroundf(value / max * 100.0f));
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
  }
  if (lv_bar_get_value(bar) != pct) lv_bar_set_value(bar, pct, LV_ANIM_OFF);
}

void fmt_num(char *buf, size_t n, float v, int decimals) {
  if (!telemetry::has(v)) {
    snprintf(buf, n, "--");
    return;
  }
  snprintf(buf, n, "%.*f", decimals, static_cast<double>(v));
}

void fmt_temp(char *buf, size_t n, float v) {
  if (!telemetry::has(v)) {
    snprintf(buf, n, "-- °C");
    return;
  }
  snprintf(buf, n, "%.1f °C", static_cast<double>(v));
}

void fmt_pct(char *buf, size_t n, float v) {
  if (!telemetry::has(v)) {
    snprintf(buf, n, "-- %%");
    return;
  }
  snprintf(buf, n, "%.0f %%", static_cast<double>(v));
}

void fmt_ghz(char *buf, size_t n, float mhz) {
  if (!telemetry::has(mhz)) {
    snprintf(buf, n, "-- GHz");
    return;
  }
  snprintf(buf, n, "%.2f GHz", static_cast<double>(mhz / 1000.0f));
}

void fmt_watts(char *buf, size_t n, float w) {
  if (!telemetry::has(w)) {
    snprintf(buf, n, "-- W");
    return;
  }
  snprintf(buf, n, "%.0f W", static_cast<double>(w));
}

void fmt_gib(char *buf, size_t n, float mib) {
  if (!telemetry::has(mib)) {
    snprintf(buf, n, "-- GB");
    return;
  }
  snprintf(buf, n, "%.1f GB", static_cast<double>(mib / 1024.0f));
}

int temp_level(float t, float warn, float crit) {
  if (!telemetry::has(t)) return -1;
  if (t >= crit) return 2;
  if (t >= warn) return 1;
  return 0;
}

void chart_fill(lv_obj_t *chart, lv_chart_series_t *ser, const void *history) {
  const auto *h = static_cast<const telemetry::History60 *>(history);
  const int cap = telemetry::History60::kCapacity;
  const int n = h->size();
  lv_coord_t *ys = lv_chart_get_y_array(chart, ser);
  // Right-align: newest sample at the right edge, empty slots on the left are "no point".
  for (int i = 0; i < cap; i++) {
    const int idx = i - (cap - n);
    const float v = idx >= 0 ? h->at(idx) : telemetry::kMissing;
    ys[i] = telemetry::has(v) ? static_cast<lv_coord_t>(lroundf(v)) : LV_CHART_POINT_NONE;
  }
  lv_chart_refresh(chart);
}

}  // namespace ui
