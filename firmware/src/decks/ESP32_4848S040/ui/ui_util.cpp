#include <math.h>
#include <stdio.h>
#include <string.h>

#include "telemetry/history.h"
#include "ui/ui_internal.h"

namespace ui {

lv_obj_t *make_page(lv_obj_t *parent, bool scrollable) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_hor(p, theme::kMargin, 0);
  lv_obj_set_style_pad_ver(p, 10, 0);
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
