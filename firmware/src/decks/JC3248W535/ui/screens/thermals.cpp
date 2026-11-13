// Thermals: dynamic list of the temperatures the PC actually reports (CPU package / core max,
// GPU core, extra agent sensors such as GPU memory junction, SSD), available ones first, plus a
// 60 s CPU/GPU history with the period maximum. Thresholds (warning/critical) only apply to
// CPU and GPU sensors; other sensors are shown neutrally (spec §6: never apply CPU limits to SSD).

#include <stdio.h>
#include <string.h>

#include "app/state_store.h"
#include "ui/ui_internal.h"

namespace ui {
namespace thermals {
namespace {

using namespace theme;

constexpr int kRows = 6;
constexpr float kBarMaxC = 110.0f;

struct Row {
  lv_obj_t *root = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *value = nullptr;
  lv_obj_t *status = nullptr;
  lv_obj_t *bar = nullptr;
};

enum class Kind : uint8_t { Cpu, Gpu, Neutral };

struct Item {
  const char *name;
  float temp;
  Kind kind;
  bool stale;
};

Row s_rows[kRows];
lv_obj_t *s_hint = nullptr;
lv_obj_t *s_sel = nullptr;
lv_obj_t *s_max = nullptr;
lv_obj_t *s_chart = nullptr;
lv_chart_series_t *s_ser = nullptr;
bool s_show_gpu = false;
bool s_user_selected = false;  // once the user picks CPU/GPU we stop auto-selecting
uint32_t s_last_rx = 0xFFFFFFFF;

void select_series(bool gpu) {
  s_show_gpu = gpu;
  lv_btnmatrix_set_btn_ctrl(s_sel, gpu ? 1 : 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_chart_set_series_color(s_chart, s_ser, theme::c(gpu ? kGpu : kCpu));
  s_last_rx = 0xFFFFFFFF;
}

Row make_row(lv_obj_t *parent) {
  Row r;
  r.root = lv_obj_create(parent);
  lv_obj_remove_style_all(r.root);
  lv_obj_set_size(r.root, LV_PCT(100), 30);
  lv_obj_clear_flag(r.root, LV_OBJ_FLAG_SCROLLABLE);
  r.name = make_label(r.root, font_small(), kMuted, "");
  lv_obj_set_width(r.name, 166);
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_DOT);
  lv_obj_align(r.name, LV_ALIGN_TOP_LEFT, 0, 4);
  r.status = make_label(r.root, font_small(), kOk, "");
  lv_obj_align(r.status, LV_ALIGN_TOP_LEFT, 172, 4);
  r.value = make_label(r.root, font_body(), kText, "--");
  lv_obj_align(r.value, LV_ALIGN_TOP_RIGHT, 0, 2);
  r.bar = make_bar(r.root, kOk, 4);
  lv_obj_set_width(r.bar, LV_PCT(100));
  lv_obj_align(r.bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  return r;
}

void fill_row(Row &r, const Item &it, const app::AppState &st) {
  lv_obj_clear_flag(r.root, LV_OBJ_FLAG_HIDDEN);
  set_text(r.name, it.name);
  char buf[24];
  fmt_temp(buf, sizeof(buf), it.temp);
  set_text(r.value, telemetry::has(it.temp) ? buf : "");  // "not available" says it; no "-- °C" overlap
  if (!telemetry::has(it.temp)) {
    set_text(r.status, tr(Str::NotAvailable));
    set_color(r.status, kMuted);
    set_color(r.value, kMuted);
    set_bar(r.bar, telemetry::kMissing, kBarMaxC);
    return;
  }
  set_color(r.value, kText);
  int lvl = 0;
  const char *txt = "";
  if (it.kind != Kind::Neutral) {
    const float w = it.kind == Kind::Cpu ? st.settings.cpu_warn_c : st.settings.gpu_warn_c;
    const float c = it.kind == Kind::Cpu ? st.settings.cpu_crit_c : st.settings.gpu_crit_c;
    lvl = temp_level(it.temp, w, c);
    txt = lvl == 2 ? tr(Str::StatusCritical) : lvl == 1 ? tr(Str::StatusHigh) : tr(Str::StatusOk);
  }
  if (it.stale) txt = tr(Str::StateStale);
  set_text(r.status, txt);
  const uint32_t col = it.kind == Kind::Neutral ? kCpu : level_color(lvl);
  set_color(r.status, it.stale ? kWarning : col);
  lv_obj_set_style_bg_color(r.bar, theme::c(it.kind == Kind::Gpu && lvl == 0 ? kGpu : col), LV_PART_INDICATOR);
  set_bar(r.bar, it.temp, kBarMaxC);
}

void sel_cb(lv_event_t *e) {
  lv_obj_t *bm = lv_event_get_target(e);
  s_user_selected = true;
  select_series(lv_btnmatrix_get_selected_btn(bm) == 1);
  update();
}

const char *s_sel_map[] = {"CPU", "GPU", ""};

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  lv_obj_t *page = make_page(parent, false);

  lv_obj_t *card = make_card(page);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 2, 0);
  lv_obj_set_style_pad_ver(card, 6, 0);
  for (auto &r : s_rows) r = make_row(card);
  s_hint = make_label(card, font_small(), kWarning, tr(Str::NeedsAdmin));
  lv_obj_set_width(s_hint, LV_PCT(100));
  lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
  lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *head = lv_obj_create(page);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), 34);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  s_sel = lv_btnmatrix_create(head);
  lv_btnmatrix_set_map(s_sel, s_sel_map);
  lv_btnmatrix_set_btn_ctrl_all(s_sel, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(s_sel, true);
  lv_btnmatrix_set_btn_ctrl(s_sel, 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(s_sel, 132, 34);
  lv_obj_set_style_pad_all(s_sel, 3, 0);
  lv_obj_set_style_pad_gap(s_sel, 3, 0);
  lv_obj_set_style_bg_color(s_sel, theme::c(kSurface), 0);
  lv_obj_set_style_border_color(s_sel, theme::c(kBorder), 0);
  lv_obj_set_style_radius(s_sel, 10, 0);
  lv_obj_set_style_bg_color(s_sel, theme::c(kSurfaceHi), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_sel, theme::c(kBorder), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(s_sel, theme::c(kText), LV_PART_ITEMS);
  lv_obj_set_style_text_font(s_sel, font_small(), LV_PART_ITEMS);
  lv_obj_set_style_radius(s_sel, 8, LV_PART_ITEMS);
  lv_obj_align(s_sel, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(s_sel, sel_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  s_max = make_label(head, font_small(), kMuted, "");
  lv_obj_align(s_max, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t *cc = make_card(page);
  lv_obj_set_width(cc, LV_PCT(100));
  lv_obj_set_flex_grow(cc, 1);
  lv_obj_set_style_pad_all(cc, 8, 0);
  lv_obj_t *ymax = make_label(cc, font_small(), kMuted, "110");
  lv_obj_align(ymax, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *ymin = make_label(cc, font_small(), kMuted, "20");
  lv_obj_align(ymin, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  s_chart = make_chart(cc, telemetry::kHistorySamples, 110);
  lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 20, 110);
  lv_obj_set_width(s_chart, 320 - 2 * kMargin - 16 - 30);
  lv_obj_align(s_chart, LV_ALIGN_TOP_RIGHT, 0, 0);
  s_ser = lv_chart_add_series(s_chart, theme::c(kCpu), LV_CHART_AXIS_PRIMARY_Y);
  lv_obj_update_layout(page);
  lv_obj_set_height(s_chart, lv_obj_get_content_height(cc));
  return page;
}

void update() {
  const auto &st = app::state();
  const auto &s = st.snap;
  const bool have = st.has_snapshot;
  const float miss = telemetry::kMissing;

  // Build the list: available sensors first, then the unavailable CPU/GPU ones (so the user sees
  // what is missing), capped at kRows.
  Item items[3 + telemetry::kMaxTemps + telemetry::kMaxStorage];
  int n = 0;
  items[n++] = {tr(Str::CpuPackage), have ? s.cpu.temp_c : miss, Kind::Cpu, false};
  items[n++] = {tr(Str::CpuCoreMax), have ? s.cpu.core_max_temp_c : miss, Kind::Cpu, false};
  items[n++] = {tr(Str::GpuCore), have ? s.gpu.temp_c : miss, Kind::Gpu, false};
  if (have) {
    for (int i = 0; i < s.temp_count; i++) {
      const bool gpu = strncmp(s.temps[i].name, "GPU", 3) == 0;
      items[n++] = {s.temps[i].name, s.temps[i].temp_c, gpu ? Kind::Gpu : Kind::Neutral, s.temps[i].stale};
    }
    for (int i = 0; i < s.storage_count; i++) items[n++] = {s.storage[i].name, s.storage[i].temp_c, Kind::Neutral, false};
  }
  int row = 0;
  for (int pass = 0; pass < 2 && row < kRows; pass++) {
    for (int i = 0; i < n && row < kRows; i++) {
      const bool avail = telemetry::has(items[i].temp);
      if ((pass == 0) == avail) fill_row(s_rows[row++], items[i], st);
    }
  }
  const int visible = row;
  for (; row < kRows; row++) lv_obj_add_flag(s_rows[row].root, LV_OBJ_FLAG_HIDDEN);
  static int s_prev_visible = -1;

  // Explain a missing CPU temperature when the agent says it lacks privileges.
  const bool cpu_missing = have && !telemetry::has(s.cpu.temp_c);
  const bool admin_note = st.agent_note_count && strstr(st.agent_first_note, "administrator");
  const bool show_hint = cpu_missing && admin_note;
  if (show_hint)
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

  // The sensor card height changes with the number of rows: let the chart take the rest.
  const int layout_key = visible * 2 + (show_hint ? 1 : 0);
  if (layout_key != s_prev_visible) {
    s_prev_visible = layout_key;
    lv_obj_t *cc = lv_obj_get_parent(s_chart);
    lv_obj_update_layout(lv_obj_get_parent(cc));
    const lv_coord_t h = lv_obj_get_content_height(cc);
    lv_obj_set_height(s_chart, h > 20 ? h : 20);
  }

  // Default the history to whichever source actually has data (CPU temp may be unavailable).
  if (!s_user_selected) {
    const bool cpu_has = telemetry::has(st.cpu_temp.max());
    const bool gpu_has = telemetry::has(st.gpu_temp.max());
    const bool want_gpu = !cpu_has && gpu_has;
    if (want_gpu != s_show_gpu) select_series(want_gpu);
  }
  const auto &hist = s_show_gpu ? st.gpu_temp : st.cpu_temp;
  char t[24], line[48];
  fmt_temp(t, sizeof(t), hist.max());
  snprintf(line, sizeof(line), "%s: %s", tr(Str::Max60s), t);
  set_text(s_max, line);

  if (st.rx_count != s_last_rx) {
    s_last_rx = st.rx_count;
    chart_fill(s_chart, s_ser, &hist);
  }
}

}  // namespace thermals
}  // namespace ui
