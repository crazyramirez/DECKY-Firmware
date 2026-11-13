// CPU / GPU detail: hardware name, sensor rows (only those the snapshot provides values for are
// filled; others show "--"), and a 60 s load + temperature chart.

#include <stdio.h>

#include "app/state_store.h"
#include "ui/ui_internal.h"

namespace ui {
namespace detail {
namespace {

using namespace theme;

constexpr int kRows = 5;
lv_obj_t *s_title = nullptr;
lv_obj_t *s_name = nullptr;
lv_obj_t *s_key[kRows] = {};
lv_obj_t *s_val[kRows] = {};
lv_obj_t *s_chart = nullptr;
lv_chart_series_t *s_ser_load = nullptr;
lv_chart_series_t *s_ser_temp = nullptr;
bool s_gpu = false;
uint32_t s_last_rx = 0xFFFFFFFF;

void back_cb(lv_event_t *) { navigate(Page::Home); }

// Value row: missing values read "--" in a dim colour so they never look like real data.
void set_val(int i, const char *text, bool known) {
  set_text(s_val[i], known ? text : "--");
  set_color(s_val[i], known ? kText : kBorder);
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  lv_obj_t *page = make_page(parent, false);

  lv_obj_t *head = lv_obj_create(page);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), kTouchMin);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *back = make_button(head, "");
  lv_label_set_text_fmt(lv_obj_get_child(back, 0), "%s %s", LV_SYMBOL_LEFT, tr(Str::Back));
  lv_obj_set_size(back, 104, kTouchMin);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
  s_title = make_label(head, font_title(), kCpu, "CPU");
  lv_obj_align(s_title, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t *card = make_card(page);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 4, 0);
  s_name = make_label(card, font_body(), kText, "");
  lv_obj_set_width(s_name, LV_PCT(100));
  lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
  for (int i = 0; i < kRows; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 22);
    s_key[i] = make_label(row, font_small(), kMuted, "");
    lv_obj_align(s_key[i], LV_ALIGN_LEFT_MID, 0, 0);
    s_val[i] = make_label(row, font_body(), kText, "--");
    lv_obj_align(s_val[i], LV_ALIGN_RIGHT_MID, 0, 0);
  }

  lv_obj_t *cc = make_card(page);
  lv_obj_set_width(cc, LV_PCT(100));
  lv_obj_set_flex_grow(cc, 1);
  lv_obj_set_style_pad_all(cc, 8, 0);
  lv_obj_t *ymax = make_label(cc, font_small(), kMuted, "100");
  lv_obj_align(ymax, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *ymin = make_label(cc, font_small(), kMuted, "0");
  lv_obj_align(ymin, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  s_chart = make_chart(cc, telemetry::kHistorySamples, 100);
  lv_obj_set_width(s_chart, kScreenW - 2 * kMargin - 16 - 30);
  lv_obj_align(s_chart, LV_ALIGN_TOP_RIGHT, 0, 0);
  s_ser_load = lv_chart_add_series(s_chart, theme::c(kCpu), LV_CHART_AXIS_PRIMARY_Y);
  s_ser_temp = lv_chart_add_series(s_chart, theme::c(kPower), LV_CHART_AXIS_PRIMARY_Y);
  lv_obj_update_layout(page);
  lv_obj_set_height(s_chart, lv_obj_get_content_height(cc));
  return page;
}

void show(bool gpu) {
  s_gpu = gpu;
  set_text(s_title, gpu ? tr(Str::Gpu) : tr(Str::Cpu));
  set_color(s_title, gpu ? kGpu : kCpu);
  lv_chart_set_series_color(s_chart, s_ser_load, theme::c(gpu ? kGpu : kCpu));
  const Str keys_cpu[kRows] = {Str::DetailSensor, Str::Load, Str::Temp, Str::Freq, Str::CpuPackagePower};
  const Str keys_gpu[kRows] = {Str::DetailSensor, Str::Load, Str::Temp, Str::Vram, Str::PowerRow};
  for (int i = 0; i < kRows; i++) set_text(s_key[i], tr(gpu ? keys_gpu[i] : keys_cpu[i]));
  s_last_rx = 0xFFFFFFFF;
  update();
}

void update() {
  if (current_page() != Page::DetailCpu && current_page() != Page::DetailGpu) return;
  const auto &st = app::state();
  const auto &s = st.snap;
  const bool have = st.has_snapshot;
  char buf[48], a[20], b[20];

  using telemetry::has;
  if (!s_gpu) {
    set_text(s_name, have && s.cpu.name[0] ? s.cpu.name : "--");
    // The sensor actually feeding the temperature value (chosen by the agent, e.g. "CPU (EC)").
    const bool t = have && has(s.cpu.temp_c);
    set_val(0, s.cpu.temp_sensor_name[0] ? s.cpu.temp_sensor_name : "CPU Package", t);
    fmt_pct(buf, sizeof(buf), s.cpu.load_pct);
    set_val(1, buf, have && has(s.cpu.load_pct));
    fmt_temp(buf, sizeof(buf), s.cpu.temp_c);
    set_val(2, buf, t);
    fmt_ghz(buf, sizeof(buf), s.cpu.freq_mhz);
    set_val(3, buf, have && has(s.cpu.freq_mhz));
    fmt_watts(buf, sizeof(buf), s.cpu.package_power_w);
    set_val(4, buf, have && has(s.cpu.package_power_w));
  } else {
    set_text(s_name, have && s.gpu.name[0] ? s.gpu.name : "--");
    const bool t = have && has(s.gpu.temp_c);
    set_val(0, "GPU Core", t);
    fmt_pct(buf, sizeof(buf), s.gpu.load_pct);
    set_val(1, buf, have && has(s.gpu.load_pct));
    fmt_temp(buf, sizeof(buf), s.gpu.temp_c);
    set_val(2, buf, t);
    fmt_gib(a, sizeof(a), s.gpu.vram_used_mib);
    fmt_gib(b, sizeof(b), s.gpu.vram_total_mib);
    snprintf(buf, sizeof(buf), "%s / %s", a, b);
    set_val(3, buf, have && has(s.gpu.vram_used_mib) && has(s.gpu.vram_total_mib));
    fmt_watts(buf, sizeof(buf), s.gpu.board_power_w);
    set_val(4, buf, have && has(s.gpu.board_power_w));
  }

  if (st.rx_count != s_last_rx) {
    s_last_rx = st.rx_count;
    chart_fill(s_chart, s_ser_load, s_gpu ? &st.gpu_load : &st.cpu_load);
    chart_fill(s_chart, s_ser_temp, s_gpu ? &st.gpu_temp : &st.cpu_temp);
  }
}

}  // namespace detail
}  // namespace ui
