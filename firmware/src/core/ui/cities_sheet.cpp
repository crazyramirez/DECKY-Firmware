// The Cities sheet, shared by every deck (ui/cities_ui.h); metrics and fonts come from the deck's
// cities::layout(). Full screen on the top layer, like the city search it opens to add a city.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/cities.h"
#include "app/state_store.h"
#include "ui/cities_ui.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace cities {
namespace {

using namespace theme;

lv_obj_t *s_modal = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_edit_lbl = nullptr;
lv_obj_t *s_row_time[storage::kMaxCities] = {};
lv_obj_t *s_row_sub[storage::kMaxCities] = {};
bool s_edit = false;
bool s_render_pending = false;
int s_last_min = -1;

intptr_t idx_of(lv_event_t *e) { return reinterpret_cast<intptr_t>(lv_event_get_user_data(e)); }
void *as_user(int i) { return reinterpret_cast<void *>(static_cast<intptr_t>(i)); }

lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

int text_px(const char *s, const lv_font_t *f) {
  lv_point_t sz;
  lv_txt_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x;
}

// Second line of a row: "Main" for the main place, else its day and how far ahead its clock is
// ("Tomorrow · +8 h"); until that is known, what the forecast is doing.
void sub_text(int i, char *buf, size_t n) {
  if (i == 0) {
    snprintf(buf, n, LV_SYMBOL_HOME "  %s", tr(Str::CityMain));
    return;
  }
  int minutes = 0;
  if (!app::city_offset(i, minutes)) {
    const char *err = app::city_error(i);
    snprintf(buf, n, "%s",
             err[0] ? tr(Str::WeatherFailed) : !app::data_online() ? tr(Str::WeatherOffline) : tr(Str::WeatherLoading));
    return;
  }
  char off[28];
  fmt_offset(off, sizeof(off), minutes);
  const int shift = app::city_day_shift(i);
  snprintf(buf, n, "%s  ·  %s", tr(shift > 0 ? Str::CityTomorrow : shift < 0 ? Str::CityYesterday : Str::NavToday), off);
}

void time_text(int i, char *buf, size_t n) {
  struct tm t;
  if (app::city_time(i, t))
    fmt_time(buf, n, t);
  else
    snprintf(buf, n, "--:--");
}

lv_obj_t *round_btn(lv_obj_t *parent, const Layout &L, const char *symbol, uint32_t color, lv_event_cb_t cb, int i) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, L.btn_d, L.btn_d);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(b, 4);
  lv_obj_center(make_label(b, L.f_btn, color, symbol));
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, as_user(i));
  return b;
}

void pick_cb(lv_event_t *e) {
  const int i = static_cast<int>(idx_of(e));
  close();
  app::show_city(i);
  navigate(Page::Today);
}

void remove_cb(lv_event_t *e) { app::remove_city(static_cast<int>(idx_of(e))); }
void main_cb(lv_event_t *e) { app::make_main_city(static_cast<int>(idx_of(e))); }
void change_main_cb(lv_event_t *) { place::open(place::Mode::Main); }
void add_cb(lv_event_t *) { place::open(place::Mode::Add); }

void render() {
  if (!s_modal) return;
  const Layout &L = layout();
  const int n = app::city_count();
  if (n == 0) {  // the main place was removed (USB console): nothing left to list
    close();
    return;
  }
  const int shown = app::today_city();
  lv_obj_clean(s_list);
  for (auto &l : s_row_time) l = nullptr;
  for (auto &l : s_row_sub) l = nullptr;
  set_text(s_edit_lbl, tr(s_edit ? Str::CityDone : Str::CityEdit));
  const int inner = kScreenW - 2 * L.margin - 2 * L.row_pad - 4;  // row content width (2 px border)
  const int gap = L.row_pad * 2 / 3;
  // Columns as wide as the widest text they can hold, in the clock format in use.
  const int time_w = text_px(app::state().settings.clock_12h ? "00:00 PM" : "00:00", L.f_time) + 2;
  const int temp_w = text_px("-00°", L.f_temp) + (L.stack ? 2 : 2 * gap);
  char b[64];
  for (int i = 0; i < n; i++) {
    const telemetry::Weather &w = app::city_weather(i);
    lv_obj_t *r = plain(s_list);
    lv_obj_set_size(r, LV_PCT(100), L.row_h);
    lv_obj_set_style_radius(r, L.row_radius, 0);
    lv_obj_set_style_bg_color(r, c(kSurface), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    // The city Today shows wears the accent; the others a border of their own colour (same size).
    lv_obj_set_style_border_width(r, 2, 0);
    lv_obj_set_style_border_color(r, c(i == shown ? kCpu : kSurface), 0);
    lv_obj_set_style_pad_hor(r, L.row_pad, 0);
    if (!s_edit) {
      lv_obj_set_style_bg_color(r, c(kSurfaceHi), LV_STATE_PRESSED);
      lv_obj_set_style_border_color(r, c(i == shown ? kCpu : kSurfaceHi), LV_STATE_PRESSED);
      lv_obj_add_event_cb(r, pick_cb, LV_EVENT_CLICKED, as_user(i));
    } else {
      lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *ic = make_label(r, L.f_icon, w.ok ? today::weather_color(w.code, w.day) : kBorder,
                              w.ok ? today::weather_icon(w.code, w.day) : PCD_SYM_CLOUD);
    lv_obj_set_width(ic, L.icon_w);
    lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
    // Right side: local time and temperature, or the actions while editing.
    int right = 0;
    if (!s_edit) {
      fmt_deg(b, sizeof(b), w.ok ? w.temp : NAN);
      lv_obj_t *tp = make_label(r, L.f_temp, w.ok ? kText : kMuted, b);
      lv_obj_set_style_text_align(tp, LV_TEXT_ALIGN_RIGHT, 0);
      time_text(i, b, sizeof(b));
      lv_obj_t *tm = make_label(r, L.f_time, kText, b);
      lv_obj_set_style_text_align(tm, LV_TEXT_ALIGN_RIGHT, 0);
      s_row_time[i] = tm;
      if (L.stack) {  // one column: the time on the name's line, the temperature on the second one
        right = time_w > temp_w ? time_w : temp_w;
        lv_obj_set_width(tm, right);
        lv_obj_set_width(tp, right);
        const int th = lv_font_get_line_height(L.f_time), ph = lv_font_get_line_height(L.f_temp);
        lv_obj_align(tm, LV_ALIGN_RIGHT_MID, 0, -(ph + 2) / 2);
        lv_obj_align(tp, LV_ALIGN_RIGHT_MID, 0, (th + 2) / 2);
      } else {
        lv_obj_set_width(tm, time_w);
        lv_obj_set_width(tp, temp_w);
        lv_obj_align(tp, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_align(tm, LV_ALIGN_RIGHT_MID, -temp_w, 0);
        right = temp_w + time_w;
      }
    } else if (i == 0) {
      lv_obj_align(round_btn(r, L, LV_SYMBOL_EDIT, kText, change_main_cb, 0), LV_ALIGN_RIGHT_MID, 0, 0);
      right = L.btn_d;
    } else {
      lv_obj_align(round_btn(r, L, LV_SYMBOL_TRASH, kCritical, remove_cb, i), LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_align(round_btn(r, L, LV_SYMBOL_HOME, kText, main_cb, i), LV_ALIGN_RIGHT_MID, -(L.btn_d + gap), 0);
      right = 2 * L.btn_d + gap;
    }
    // Name over the second line, both centred as a block.
    const int text_x = L.icon_w + gap, text_w = inner - text_x - right - gap;
    const int name_h = lv_font_get_line_height(L.f_name), sub_h = lv_font_get_line_height(L.f_sub);
    fit_text(b, sizeof(b), app::city_name(i), L.f_name, text_w);
    lv_obj_t *nm = make_label(r, L.f_name, kText, b);
    lv_obj_set_size(nm, text_w + 2, name_h);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_CLIP);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, text_x, -(sub_h + 2) / 2);
    sub_text(i, b, sizeof(b));
    lv_obj_t *sb = make_label(r, L.f_sub, i == 0 ? kCpu : kMuted, b);
    lv_obj_set_size(sb, text_w, sub_h);
    lv_label_set_long_mode(sb, LV_LABEL_LONG_DOT);
    lv_obj_align(sb, LV_ALIGN_LEFT_MID, text_x, (name_h + 2) / 2);
    s_row_sub[i] = sb;
  }
  // Last row: add a city (or why not).
  if (n < storage::kMaxCities) {
    lv_obj_t *a = plain(s_list);
    lv_obj_set_size(a, LV_PCT(100), L.row_h);
    lv_obj_set_style_radius(a, L.row_radius, 0);
    lv_obj_set_style_border_width(a, 2, 0);
    lv_obj_set_style_border_color(a, c(kBorder), 0);
    lv_obj_set_style_border_opa(a, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(a, c(kSurface), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(a, LV_OPA_COVER, LV_STATE_PRESSED);
    snprintf(b, sizeof(b), LV_SYMBOL_PLUS "   %s", tr(Str::CityAdd));
    lv_obj_center(make_label(a, L.f_name, kCpu, b));
    lv_obj_add_event_cb(a, add_cb, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t *hint = make_label(s_list, L.f_sub, kMuted,
                              n >= storage::kMaxCities && !s_edit ? tr(Str::CityFull)
                              : s_edit                            ? tr(Str::CitiesEditHint)
                              : n > 1                             ? tr(Str::CitiesHint)
                                                                  : tr(Str::CitiesHintOne));
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(hint, L.gap, 0);
  lv_obj_set_style_pad_hor(hint, L.row_pad, 0);
  s_last_min = -1;
}

// Rows are rebuilt outside of the event that asked for it (a button of a row may be the caller).
void render_async(void *) {
  s_render_pending = false;
  render();
}

void request_render() {
  if (!s_modal || s_render_pending) return;
  s_render_pending = true;
  lv_async_call(render_async, nullptr);
}

void edit_cb(lv_event_t *) {
  s_edit = !s_edit;
  request_render();
}

}  // namespace

void close() {
  if (!s_modal) return;
  lv_obj_del_async(s_modal);  // we may be inside an event of one of its children
  s_modal = s_list = s_edit_lbl = nullptr;
  for (auto &l : s_row_time) l = nullptr;
  for (auto &l : s_row_sub) l = nullptr;
}

bool active() { return s_modal != nullptr; }

void open() {
  if (app::city_count() == 0) {  // nothing to list yet: straight to the search for the main place
    place::open(place::Mode::Main);
    return;
  }
  close();
  const Layout &L = layout();
  s_edit = false;
  s_modal = plain(lv_layer_top());
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_modal, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *col = plain(s_modal);
  lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(col, L.margin, 0);
  lv_obj_set_style_pad_row(col, L.gap, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

  // Header: round close button, title, "Edit" / "Done".
  lv_obj_t *head = plain(col);
  lv_obj_set_size(head, LV_PCT(100), L.head_h);
  lv_obj_t *x = lv_btn_create(head);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, L.close_d, L.close_d);
  lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(x, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(x, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(x, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(x, L.close_d / 4);
  lv_obj_align(x, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_center(make_label(x, L.f_name, kText, LV_SYMBOL_CLOSE));
  lv_obj_add_event_cb(x, [](lv_event_t *) { close(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_align(make_label(head, L.f_title, kText, tr(Str::Cities)), LV_ALIGN_LEFT_MID, L.title_x, 0);
  lv_obj_t *ed = lv_btn_create(head);
  lv_obj_remove_style_all(ed);
  lv_obj_set_size(ed, LV_SIZE_CONTENT, L.close_d);
  lv_obj_set_style_radius(ed, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(ed, L.close_d / 2, 0);
  lv_obj_set_style_bg_color(ed, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(ed, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(ed, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(ed, L.close_d / 4);
  lv_obj_align(ed, LV_ALIGN_RIGHT_MID, 0, 0);
  s_edit_lbl = make_label(ed, L.f_sub, kCpu, tr(Str::CityEdit));
  lv_obj_center(s_edit_lbl);
  lv_obj_add_event_cb(ed, edit_cb, LV_EVENT_CLICKED, nullptr);

  s_list = lv_obj_create(col);
  lv_obj_remove_style_all(s_list);
  lv_obj_set_width(s_list, LV_PCT(100));
  lv_obj_set_flex_grow(s_list, 1);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_list, L.gap, 0);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);
  render();
}

void on_weather() { request_render(); }

void dev_edit(bool on) {
  s_edit = on;
  request_render();
}

void tick() {
  if (!s_modal) return;
  struct tm t;
  const int stamp = app::local_time(t) ? t.tm_hour * 60 + t.tm_min : -2;
  if (stamp == s_last_min) return;
  s_last_min = stamp;
  char b[64];
  for (int i = 0; i < app::city_count() && i < storage::kMaxCities; i++) {
    if (s_row_time[i]) {
      time_text(i, b, sizeof(b));
      set_text(s_row_time[i], b);
    }
    if (s_row_sub[i]) {
      sub_text(i, b, sizeof(b));
      set_text(s_row_sub[i], b);
    }
  }
}

}  // namespace cities
}  // namespace ui
