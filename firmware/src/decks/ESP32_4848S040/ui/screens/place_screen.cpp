// City search for the weather (Today tab, Settings > Time & weather > Location).
// Full-screen sheet on the top layer: header, search field, results. The keyboard is shown while
// typing and hidden when a search starts, so the results get the whole screen. The search goes
// through the PC (agent "weather.search" -> Open-Meteo geocoding); picking a result stores the
// place on the deck (NVS) and fetches its forecast.

#include <stdio.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "network/net_task.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace place {
namespace {

using namespace theme;

constexpr int kKbH = 236;
lv_obj_t *s_modal = nullptr;
lv_obj_t *s_col = nullptr;
lv_obj_t *s_kb = nullptr;
lv_obj_t *s_box = nullptr;
lv_obj_t *s_ta = nullptr;
lv_obj_t *s_msg = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_spinner = nullptr;
telemetry::Place s_results[telemetry::kMaxPlaces];
int s_count = 0;

void close() {
  if (s_modal) {
    lv_obj_del_async(s_modal);  // we may be inside an event of one of its children
    s_modal = s_col = s_kb = s_box = s_ta = s_msg = s_list = s_spinner = nullptr;
  }
}

void set_msg(const char *text, uint32_t color = kMuted) {
  if (!s_msg) return;
  lv_label_set_text(s_msg, text);
  set_color(s_msg, color);
  if (text[0])
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
}

void keyboard(bool on) {
  if (!s_kb) return;
  if (on) {
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_col, 480 - kKbH);
    lv_obj_set_style_border_color(s_box, c(kCpu), 0);
  } else {
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_col, 480);
    lv_obj_set_style_border_color(s_box, c(kBorder), 0);
  }
}

void search() {
  char q[64];
  strlcpy(q, lv_textarea_get_text(s_ta), sizeof(q));
  // trim
  char *p = q;
  while (*p == ' ') p++;
  size_t n = strlen(p);
  while (n && p[n - 1] == ' ') p[--n] = '\0';
  if (n < 2) {
    set_msg(tr(Str::PlaceTypeHint));
    return;
  }
  keyboard(false);
  lv_obj_clean(s_list);
  s_count = 0;
  lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  set_msg(tr(Str::PlaceSearching));
  net::search_places(p, lang() == storage::Lang::En ? "en" : "es");
}

void pick_cb(lv_event_t *e) {
  const int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= s_count) return;
  const telemetry::Place pl = s_results[i];
  close();
  app::set_place(pl.name, pl.lat, pl.lon);
  char b[64];
  snprintf(b, sizeof(b), PCD_SYM_PIN "  %s", pl.name);
  toast(b, kOk);
  navigate(Page::Today);
}

void render() {
  if (!s_list) return;
  lv_obj_clean(s_list);
  for (int i = 0; i < s_count; i++) {
    const auto &pl = s_results[i];
    lv_obj_t *r = lv_obj_create(s_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), 58);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(r, 12, 0);
    lv_obj_set_style_bg_color(r, c(kSurface), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, c(kSurfaceHi), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(r, 12, 0);
    lv_obj_t *ic = make_label(r, font_body(), kCpu, PCD_SYM_PIN);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *nm = make_label(r, font_body(), kText, pl.name);
    lv_obj_set_width(nm, 380);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 28, 9);
    lv_obj_t *rg = make_label(r, font_small(), kMuted, pl.region);
    lv_obj_set_width(rg, 380);
    lv_label_set_long_mode(rg, LV_LABEL_LONG_DOT);
    lv_obj_align(rg, LV_ALIGN_BOTTOM_LEFT, 28, -9);
    lv_obj_add_event_cb(r, pick_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
}

void ta_cb(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    search();
  } else if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
    keyboard(true);
  }
}

void kb_cb(lv_event_t *e) {
  // OK / Enter reach the text area as LV_EVENT_READY too (handled there, once).
  if (lv_event_get_code(e) == LV_EVENT_CANCEL) keyboard(false);
}

}  // namespace

void open() {
  close();
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_modal);
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_modal, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

  s_col = lv_obj_create(s_modal);
  lv_obj_remove_style_all(s_col);
  lv_obj_set_size(s_col, LV_PCT(100), 480 - kKbH);
  lv_obj_set_style_pad_all(s_col, kMargin, 0);
  lv_obj_set_style_pad_row(s_col, 10, 0);
  lv_obj_set_flex_flow(s_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(s_col, LV_OBJ_FLAG_SCROLLABLE);

  // Header: round close button, title, subtitle.
  lv_obj_t *head = lv_obj_create(s_col);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), kTouchMin);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *x = lv_btn_create(head);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, 40, 40);
  lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(x, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(x, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(x, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(x, 8);
  lv_obj_align(x, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *xl = make_label(x, font_body(), kText, LV_SYMBOL_CLOSE);
  lv_obj_center(xl);
  lv_obj_add_event_cb(x, [](lv_event_t *) { close(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *t = make_label(head, font_title(), kText, tr(Str::Location));
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 54, -1);
  lv_obj_t *st = make_label(head, font_small(), kMuted, tr(Str::PlaceSubtitle));
  lv_obj_set_size(st, 380, lv_font_get_line_height(font_small()));  // one line, "..." if longer
  lv_label_set_long_mode(st, LV_LABEL_LONG_DOT);
  lv_obj_align(st, LV_ALIGN_BOTTOM_LEFT, 54, 1);

  // Search field.
  s_box = lv_obj_create(s_col);
  lv_obj_remove_style_all(s_box);
  lv_obj_set_size(s_box, LV_PCT(100), 50);
  lv_obj_set_style_radius(s_box, 12, 0);
  lv_obj_set_style_bg_color(s_box, c(kSurface), 0);
  lv_obj_set_style_bg_opa(s_box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_box, c(kCpu), 0);
  lv_obj_set_style_border_width(s_box, 2, 0);
  lv_obj_set_style_pad_left(s_box, 14, 0);
  lv_obj_set_style_pad_right(s_box, 6, 0);
  lv_obj_set_flex_flow(s_box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_box, 10, 0);
  lv_obj_clear_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
  make_label(s_box, font_body(), kMuted, PCD_SYM_SEARCH);
  s_ta = lv_textarea_create(s_box);
  lv_textarea_set_one_line(s_ta, true);
  lv_textarea_set_max_length(s_ta, 40);
  lv_textarea_set_placeholder_text(s_ta, tr(Str::PlaceSearchHint));
  lv_obj_set_flex_grow(s_ta, 1);
  lv_obj_set_style_bg_opa(s_ta, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_ta, 0, 0);
  lv_obj_set_style_border_width(s_ta, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(s_ta, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_pad_all(s_ta, 0, 0);
  lv_obj_set_style_text_font(s_ta, font_body(), 0);
  lv_obj_set_style_text_color(s_ta, c(kText), 0);
  lv_obj_set_style_text_color(s_ta, c(kMuted), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_border_color(s_ta, c(kCpu), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_event_cb(s_ta, ta_cb, LV_EVENT_ALL, nullptr);
  lv_obj_add_state(s_ta, LV_STATE_FOCUSED);

  // Message (hint / searching / errors) + spinner, then the results.
  lv_obj_t *mrow = lv_obj_create(s_col);
  lv_obj_remove_style_all(mrow);
  lv_obj_set_size(mrow, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(mrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(mrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(mrow, 8, 0);
  lv_obj_set_style_pad_hor(mrow, 4, 0);
  s_spinner = lv_spinner_create(mrow, 1000, 60);
  lv_obj_set_size(s_spinner, 18, 18);
  lv_obj_set_style_arc_width(s_spinner, 3, 0);
  lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_spinner, c(kSurfaceHi), 0);
  lv_obj_set_style_arc_color(s_spinner, c(kCpu), LV_PART_INDICATOR);
  lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  s_msg = make_label(mrow, font_small(), kMuted, "");
  lv_obj_set_flex_grow(s_msg, 1);
  lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
  set_msg(tr(Str::PlaceTypeHint));

  s_list = lv_obj_create(s_col);
  lv_obj_remove_style_all(s_list);
  lv_obj_set_width(s_list, LV_PCT(100));
  lv_obj_set_flex_grow(s_list, 1);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_list, 8, 0);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);

  // Keyboard: same look as the Wi-Fi sheet.
  s_kb = lv_keyboard_create(s_modal);
  lv_obj_set_size(s_kb, LV_PCT(100), kKbH);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(s_kb, c(0x0E141F), 0);
  lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_kb, 0, 0);
  lv_obj_set_style_radius(s_kb, 0, 0);
  lv_obj_set_style_pad_all(s_kb, 6, 0);
  lv_obj_set_style_pad_gap(s_kb, 5, 0);
  lv_obj_set_style_text_font(s_kb, font_body(), LV_PART_ITEMS);
  lv_obj_set_style_text_color(s_kb, c(kText), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_kb, c(kSurfaceHi), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_radius(s_kb, 8, LV_PART_ITEMS);
  lv_obj_set_style_border_width(s_kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(s_kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_kb, c(kBorder), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(s_kb, c(kSurface), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(s_kb, c(kMuted), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_keyboard_set_textarea(s_kb, s_ta);
  lv_obj_add_event_cb(s_kb, kb_cb, LV_EVENT_ALL, nullptr);
  s_count = 0;
}

void dev_search(const char *query) {
  open();
  if (query && query[0]) {
    lv_textarea_set_text(s_ta, query);
    search();
  }
}

void on_results() {
  if (!s_modal) return;
  const int n = net::place_results(s_results, telemetry::kMaxPlaces);
  if (n == net::kPlacesSearching) return;
  lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  s_count = n > 0 ? n : 0;
  if (n == net::kPlacesOffline)
    set_msg(tr(Str::PlaceOffline), kWarning);
  else if (n == net::kPlacesFailed)
    set_msg(tr(Str::PlaceFailed), kWarning);
  else if (n == 0)
    set_msg(tr(Str::PlaceNone));
  else
    set_msg("");
  render();
}

}  // namespace place
}  // namespace ui
