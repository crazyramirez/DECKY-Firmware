// Dashboard: 2x2 metric cards (CPU, GPU, RAM, power) + laptop battery strip (only when the PC
// has a battery) + 60 s CPU chart filling the rest.
//
// Cards degrade honestly: when a temperature is unavailable the card shows the load as its main
// value and says "temp. unavailable"; the power card falls back from CPU package power to GPU
// power, always labelled with its real source (never "PC consumption").

#include <stdio.h>

#include "app/state_store.h"
#include "ui/ui_internal.h"

namespace ui {
namespace dashboard {
namespace {

using namespace theme;

struct Card {
  lv_obj_t *root = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *value = nullptr;
  lv_obj_t *sub = nullptr;
  lv_obj_t *bar = nullptr;
};

Card s_cpu, s_gpu, s_ram, s_pwr;
lv_obj_t *s_chart = nullptr;
lv_chart_series_t *s_ser_load = nullptr;
lv_chart_series_t *s_ser_temp = nullptr;
lv_obj_t *s_legend_temp = nullptr;
lv_obj_t *s_now = nullptr;
uint32_t s_last_rx = 0xFFFFFFFF;
lv_obj_t *s_page = nullptr;
lv_obj_t *s_chart_card = nullptr;

struct BatteryStrip {
  lv_obj_t *root = nullptr;
  lv_obj_t *icon = nullptr;
  lv_obj_t *pct = nullptr;
  lv_obj_t *status = nullptr;
  lv_obj_t *energy = nullptr;
  lv_obj_t *bar = nullptr;
};
BatteryStrip s_bat;
constexpr int kBatH = 52;

constexpr int kCardW = (320 - 2 * kMargin - kGap) / 2;  // 144
constexpr int kCardH = 94;  // compact enough to leave the chart room under the battery strip
constexpr int kAxisW = 28;

void card_click_cb(lv_event_t *e) {
  const auto p = static_cast<Page>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  navigate(p);
}

Card make_metric_card(lv_obj_t *parent, const char *title, uint32_t accent, bool bar, Page target = Page::Home) {
  Card c;
  c.root = make_card(parent);
  lv_obj_set_size(c.root, kCardW, kCardH);
  if (target != Page::Home) {
    lv_obj_add_flag(c.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c.root, theme::c(kSurfaceHi), LV_STATE_PRESSED);
    lv_obj_add_event_cb(c.root, card_click_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(target)));
    lv_obj_t *chev = make_label(c.root, font_small(), kMuted, LV_SYMBOL_RIGHT);
    lv_obj_align(chev, LV_ALIGN_TOP_RIGHT, 0, 0);
  }
  c.title = make_label(c.root, font_small(), accent, title);
  lv_obj_set_style_text_letter_space(c.title, 1, 0);
  lv_obj_set_pos(c.title, 0, 0);
  c.value = make_label(c.root, font_value(), kText, "--");
  lv_obj_set_pos(c.value, 0, 15);
  c.sub = make_label(c.root, font_small(), kMuted, "");
  lv_obj_set_width(c.sub, kCardW - 20);
  lv_label_set_long_mode(c.sub, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(c.sub, 0, 45);
  if (bar) {
    c.bar = make_bar(c.root, accent, 4);
    lv_obj_set_width(c.bar, kCardW - 20);
    lv_obj_set_pos(c.bar, 0, 70);  // ~9 px of air under the subtitle
  }
  return c;
}

lv_obj_t *legend(lv_obj_t *parent, uint32_t color, const char *text) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_t *sw = lv_obj_create(row);
  lv_obj_remove_style_all(sw);
  lv_obj_set_size(sw, 10, 3);
  lv_obj_set_style_bg_color(sw, theme::c(color), 0);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
  make_label(row, font_small(), kMuted, text);
  return row;
}

// Join the available parts with " · "; missing parts are simply left out.
void join(char *out, size_t n, const char *a, const char *b) {
  if (a[0] && b[0])
    snprintf(out, n, "%s · %s", a, b);
  else
    snprintf(out, n, "%s", a[0] ? a : b);
}

void update_temp_card(Card &c, float temp, float warn, float crit, float load, const char *detail) {
  char v[24], sub[64], l[16];
  if (telemetry::has(temp)) {
    fmt_temp(v, sizeof(v), temp);
    const int lvl = temp_level(temp, warn, crit);
    set_color(c.value, lvl > 0 ? level_color(lvl) : kText);
    l[0] = '\0';
    if (telemetry::has(load)) fmt_pct(l, sizeof(l), load);
    join(sub, sizeof(sub), l, detail);
  } else if (telemetry::has(load)) {
    fmt_pct(v, sizeof(v), load);  // load becomes the headline when there is no temperature
    set_color(c.value, kText);
    join(sub, sizeof(sub), tr(Str::TempUnavailable), detail);
  } else {
    snprintf(v, sizeof(v), "--");
    set_color(c.value, kText);
    snprintf(sub, sizeof(sub), "%s", tr(Str::NoSensor));
  }
  set_text(c.value, v);
  set_text(c.sub, sub[0] ? sub : tr(Str::NoSensor));
  set_bar(c.bar, load, 100);
}

// Chart height follows the space left by the (optional) battery strip.
void fit_chart() {
  lv_obj_update_layout(s_page);
  const lv_coord_t h = lv_obj_get_content_height(s_chart_card) - 30;
  lv_obj_set_height(s_chart, h > 20 ? h : 20);
}

void make_battery_strip(lv_obj_t *page) {
  BatteryStrip &b = s_bat;
  b.root = make_card(page);
  lv_obj_set_size(b.root, LV_PCT(100), kBatH);
  lv_obj_set_style_pad_ver(b.root, 5, 0);
  lv_obj_set_style_pad_hor(b.root, 12, 0);
  // [icon 79 %]                 [Cargando · 45 W]
  //                              [62.7 / 79 Wh]
  // [=============== charge bar ===============]
  b.icon = make_label(b.root, font_title(), kOk, LV_SYMBOL_BATTERY_FULL);
  lv_obj_align(b.icon, LV_ALIGN_LEFT_MID, 0, -5);
  b.pct = make_label(b.root, font_title(), kText, "");
  lv_obj_align(b.pct, LV_ALIGN_LEFT_MID, 36, -5);
  b.status = make_label(b.root, font_small(), kText, "");
  lv_obj_set_width(b.status, 170);
  lv_label_set_long_mode(b.status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(b.status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(b.status, LV_ALIGN_TOP_RIGHT, 0, -4);
  b.energy = make_label(b.root, font_small(), kMuted, "");
  lv_obj_align(b.energy, LV_ALIGN_TOP_RIGHT, 0, 12);  // ~10 px above the charge bar
  b.bar = make_bar(b.root, kOk, 3);
  lv_obj_set_width(b.bar, LV_PCT(100));
  lv_obj_align(b.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(b.root, LV_OBJ_FLAG_HIDDEN);
}

const char *battery_symbol(int pct) {
  return pct >= 85 ? LV_SYMBOL_BATTERY_FULL : pct >= 60 ? LV_SYMBOL_BATTERY_3 : pct >= 35 ? LV_SYMBOL_BATTERY_2
         : pct >= 12 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
}

void update_battery(bool have, const telemetry::Snapshot &s) {
  BatteryStrip &b = s_bat;
  const bool show = have && s.has_battery;
  if (show == !lv_obj_has_flag(b.root, LV_OBJ_FLAG_HIDDEN)) {
    // visibility unchanged
  } else {
    if (show)
      lv_obj_clear_flag(b.root, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(b.root, LV_OBJ_FLAG_HIDDEN);
    fit_chart();
  }
  if (!show) return;

  const int pct = s.bat_pct;
  const bool plugged = s.bat_plugged == 1;
  const uint32_t col = plugged || pct > 30 ? kOk : pct > 15 ? kWarning : kCritical;
  char buf[48], t[24];
  set_text(b.icon, s.bat_charging == 1 ? LV_SYMBOL_CHARGE : battery_symbol(pct));
  set_color(b.icon, col);
  snprintf(buf, sizeof(buf), "%d %%", pct);
  set_text(b.pct, buf);

  // Status: charging (+rate) / plugged in / time left on battery (+drain).
  const bool rate = telemetry::has(s.bat_rate_w) && (s.bat_rate_w > 0.5f || s.bat_rate_w < -0.5f);
  if (s.bat_charging == 1) {
    if (rate)
      snprintf(buf, sizeof(buf), "%s · %.0f W", tr(Str::BatCharging), static_cast<double>(s.bat_rate_w));
    else
      snprintf(buf, sizeof(buf), "%s", tr(Str::BatCharging));
  } else if (plugged) {
    snprintf(buf, sizeof(buf), "%s", tr(Str::BatPlugged));
  } else if (s.bat_secs_left >= 0) {
    snprintf(t, sizeof(t), "%d h %02d min", static_cast<int>(s.bat_secs_left / 3600),
             static_cast<int>(s.bat_secs_left % 3600 / 60));
    snprintf(buf, sizeof(buf), tr(Str::BatLeftFmt), t);
  } else if (rate) {
    snprintf(buf, sizeof(buf), "%s · %.0f W", tr(Str::BatOnBattery), static_cast<double>(-s.bat_rate_w));
  } else {
    snprintf(buf, sizeof(buf), "%s", tr(Str::BatOnBattery));
  }
  set_text(b.status, buf);

  // Remaining energy (capacity left), e.g. "62.7 / 79.4 Wh".
  if (telemetry::has(s.bat_remaining_wh) && telemetry::has(s.bat_full_wh))
    snprintf(buf, sizeof(buf), "%.1f / %.0f Wh", static_cast<double>(s.bat_remaining_wh), static_cast<double>(s.bat_full_wh));
  else if (telemetry::has(s.bat_remaining_wh))
    snprintf(buf, sizeof(buf), "%.1f Wh", static_cast<double>(s.bat_remaining_wh));
  else
    buf[0] = '\0';
  set_text(b.energy, buf);
  lv_obj_set_style_bg_color(b.bar, theme::c(col), LV_PART_INDICATOR);
  set_bar(b.bar, static_cast<float>(pct), 100);
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  lv_obj_t *page = make_page(parent, false);
  s_page = page;

  lv_obj_t *grid = lv_obj_create(page);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, LV_PCT(100), 2 * kCardH + kGap);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, kGap, 0);
  lv_obj_set_style_pad_column(grid, kGap, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  s_cpu = make_metric_card(grid, tr(Str::Cpu), kCpu, true, Page::DetailCpu);
  s_gpu = make_metric_card(grid, tr(Str::Gpu), kGpu, true, Page::DetailGpu);
  s_ram = make_metric_card(grid, tr(Str::Ram), kRam, true);
  s_pwr = make_metric_card(grid, tr(Str::Power), kPower, false);

  make_battery_strip(page);

  // Chart card fills the remaining height: header row, then axis labels + plot.
  lv_obj_t *cc = make_card(page);
  s_chart_card = cc;
  lv_obj_set_width(cc, LV_PCT(100));
  lv_obj_set_flex_grow(cc, 1);
  lv_obj_set_style_pad_all(cc, 8, 0);

  lv_obj_t *head = lv_obj_create(cc);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), 18);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 10, 0);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *t = make_label(head, font_small(), kText, tr(Str::ChartCpu60));
  lv_obj_set_flex_grow(t, 1);
  legend(head, kCpu, tr(Str::ChartLoad));
  s_legend_temp = legend(head, kPower, tr(Str::ChartTemp));

  lv_obj_t *ymax = make_label(cc, font_small(), kMuted, "100");
  lv_obj_align(ymax, LV_ALIGN_TOP_LEFT, 0, 22);
  lv_obj_t *ymin = make_label(cc, font_small(), kMuted, "0");
  lv_obj_align(ymin, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  s_now = make_label(cc, font_title(), kCpu, "");
  lv_obj_align(s_now, LV_ALIGN_TOP_RIGHT, 0, 22);

  s_chart = make_chart(cc, telemetry::kHistorySamples, 100);
  lv_obj_set_width(s_chart, 320 - 2 * kMargin - 16 - kAxisW);
  lv_obj_align(s_chart, LV_ALIGN_TOP_RIGHT, 0, 30);
  s_ser_load = lv_chart_add_series(s_chart, theme::c(kCpu), LV_CHART_AXIS_PRIMARY_Y);
  s_ser_temp = lv_chart_add_series(s_chart, theme::c(kPower), LV_CHART_AXIS_PRIMARY_Y);
  fit_chart();
  lv_obj_move_foreground(s_now);  // current value floats over the plot's top-right corner
  return page;
}

void update() {
  const auto &st = app::state();
  const auto &s = st.snap;
  const bool have = st.has_snapshot;
  const float miss = telemetry::kMissing;
  char a[24], b[24], line[64];

  // CPU: detail = frequency (when known)
  a[0] = '\0';
  if (have && telemetry::has(s.cpu.freq_mhz)) fmt_ghz(a, sizeof(a), s.cpu.freq_mhz);
  update_temp_card(s_cpu, have ? s.cpu.temp_c : miss, st.settings.cpu_warn_c, st.settings.cpu_crit_c,
                   have ? s.cpu.load_pct : miss, a);

  // GPU: detail = VRAM used (when known)
  a[0] = '\0';
  if (have && telemetry::has(s.gpu.vram_used_mib)) fmt_gib(a, sizeof(a), s.gpu.vram_used_mib);
  update_temp_card(s_gpu, have ? s.gpu.temp_c : miss, st.settings.gpu_warn_c, st.settings.gpu_crit_c,
                   have ? s.gpu.load_pct : miss, a);

  // RAM
  fmt_gib(a, sizeof(a), have ? s.ram.used_mib : miss);
  set_text(s_ram.value, a);
  if (have && telemetry::has(s.ram.total_mib) && telemetry::has(s.ram.used_mib) && s.ram.total_mib > 0) {
    fmt_gib(b, sizeof(b), s.ram.total_mib);
    snprintf(line, sizeof(line), "/ %s · %.0f %%", b, static_cast<double>(s.ram.used_mib / s.ram.total_mib * 100.0f));
  } else {
    snprintf(line, sizeof(line), "%s", tr(Str::NoSensor));
  }
  set_text(s_ram.sub, line);
  set_bar(s_ram.bar, have ? s.ram.used_mib : miss, have ? s.ram.total_mib : 0);

  // Power: CPU package first, GPU power as an explicitly labelled fallback.
  if (have && telemetry::has(s.cpu.package_power_w)) {
    fmt_watts(a, sizeof(a), s.cpu.package_power_w);
    set_text(s_pwr.sub, tr(Str::CpuPackagePower));
  } else if (have && telemetry::has(s.gpu.board_power_w)) {
    fmt_watts(a, sizeof(a), s.gpu.board_power_w);
    set_text(s_pwr.sub, tr(Str::GpuBoardPower));
  } else {
    snprintf(a, sizeof(a), "-- W");
    set_text(s_pwr.sub, tr(Str::NoSensor));
  }
  set_text(s_pwr.value, a);

  update_battery(have, s);

  // Chart: refill only when a new snapshot arrived (rx_count; real snapshots carry no seq).
  if (st.rx_count != s_last_rx) {
    s_last_rx = st.rx_count;
    chart_fill(s_chart, s_ser_load, &st.cpu_load);
    const bool temp_known = telemetry::has(st.cpu_temp.max());
    lv_chart_hide_series(s_chart, s_ser_temp, !temp_known);
    if (temp_known)
      lv_obj_clear_flag(s_legend_temp, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_legend_temp, LV_OBJ_FLAG_HIDDEN);
    chart_fill(s_chart, s_ser_temp, &st.cpu_temp);
    fmt_pct(a, sizeof(a), st.cpu_load.latest());
    set_text(s_now, have ? a : "");
  }
}

}  // namespace dashboard
}  // namespace ui
