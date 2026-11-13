// On-device Wi-Fi manager: scan, pick a network, type the password on an on-screen keyboard,
// set the agent address, forget the network. Credentials go to NVS (storage::NetConfig) and the
// network task is asked to reconnect. The password is never shown unless the user taps "Show".

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "network/net_task.h"
#include "storage/net_store.h"
#include "ui/ui_internal.h"

namespace ui {
namespace wifi {
namespace {

using namespace theme;

lv_obj_t *s_status = nullptr;
lv_obj_t *s_agent = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_scan_btn = nullptr;
lv_obj_t *s_spinner = nullptr;
lv_obj_t *s_forget = nullptr;

// Modal (password / agent editor) on the top layer.
lv_obj_t *s_modal = nullptr;
lv_obj_t *s_kb = nullptr;
lv_obj_t *s_ta_ssid = nullptr;
lv_obj_t *s_ta_pass = nullptr;


lv_obj_t *s_modal_err = nullptr;
lv_obj_t *s_fields = nullptr;  // row holding the field boxes + Connect

char s_pending_ssid[33] = "";
bool s_pending_open = false;

net::ScanEntry s_results[net::kMaxScan];

// Field boxes around the text areas (focus ring drawn on the box, not on the bare text area).
lv_obj_t *s_box_ssid = nullptr;
lv_obj_t *s_box_pass = nullptr;

void close_modal() {
  if (s_modal) {
    lv_obj_del_async(s_modal);  // deferred: we may be inside an event of one of its children
    s_modal = nullptr;
    s_kb = s_ta_ssid = s_ta_pass = s_modal_err = s_box_ssid = s_box_pass = s_fields = nullptr;
  }
}

void focus_box(lv_obj_t *ta) {
  if (s_box_ssid) lv_obj_set_style_border_color(s_box_ssid, c(ta == s_ta_ssid ? kCpu : kBorder), 0);
  if (s_box_pass) lv_obj_set_style_border_color(s_box_pass, c(ta == s_ta_pass ? kCpu : kBorder), 0);
}

void ta_focus_cb(lv_event_t *e) {
  if (!s_kb) return;
  lv_obj_t *ta = lv_event_get_target(e);
  lv_keyboard_set_textarea(s_kb, ta);
  focus_box(ta);
}

// Full-screen sheet: header row (round close button, title, subtitle), one row with the field(s)
// and the Connect button, an error line, and the keyboard along the bottom.
constexpr int kKbH = 240;

lv_obj_t *modal_base(const char *title, const char *subtitle) {
  close_modal();
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_modal);
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_modal, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *col = lv_obj_create(s_modal);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, LV_PCT(100), kScreenH - kKbH);
  lv_obj_set_style_pad_all(col, kMargin, 0);
  lv_obj_set_style_pad_row(col, 9, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *head = lv_obj_create(col);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), 48);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *x = lv_btn_create(head);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, 48, 48);
  lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(x, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(x, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(x, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(x, 12);
  lv_obj_align(x, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *xl = make_label(x, font_body(), kText, LV_SYMBOL_CLOSE);
  lv_obj_center(xl);
  lv_obj_add_event_cb(x, [](lv_event_t *) { close_modal(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *t = make_label(head, font_title(), kText, title);
  lv_obj_set_size(t, 375, lv_font_get_line_height(font_title()));
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 66, 0);
  lv_obj_t *st = make_label(head, font_small(), kMuted, subtitle);
  lv_obj_set_width(st, 240);
  lv_label_set_long_mode(st, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(st, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(st, LV_ALIGN_RIGHT_MID, 0, 0);

  s_fields = lv_obj_create(col);
  lv_obj_remove_style_all(s_fields);
  lv_obj_set_size(s_fields, LV_PCT(100), 63);
  lv_obj_set_flex_flow(s_fields, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_fields, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_fields, 9, 0);
  lv_obj_clear_flag(s_fields, LV_OBJ_FLAG_SCROLLABLE);

  // Keyboard: dark tray, rounded keys; control keys (shift, 1#, enter...) are a shade darker.
  s_kb = lv_keyboard_create(s_modal);
  lv_obj_set_size(s_kb, LV_PCT(100), kKbH);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(s_kb, c(0x0E141F), 0);
  lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_kb, 0, 0);
  lv_obj_set_style_radius(s_kb, 0, 0);
  lv_obj_set_style_pad_all(s_kb, 6, 0);
  lv_obj_set_style_pad_gap(s_kb, 6, 0);
  lv_obj_set_style_text_font(s_kb, font_body(), LV_PART_ITEMS);
  lv_obj_set_style_text_color(s_kb, c(kText), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_kb, c(kSurfaceHi), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_radius(s_kb, 12, LV_PART_ITEMS);
  lv_obj_set_style_border_width(s_kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(s_kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_kb, c(kBorder), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(s_kb, c(kSurface), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(s_kb, c(kMuted), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED);
  return col;
}

// Rounded field box with an icon on the left; the text area inside is borderless.
lv_obj_t *make_field(lv_obj_t *parent, const char *icon, const char *placeholder, size_t max_len, lv_obj_t **box_out) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_height(box, 63);
  lv_obj_set_flex_grow(box, 1);
  lv_obj_set_style_radius(box, 18, 0);
  lv_obj_set_style_bg_color(box, c(kSurface), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(box, c(kBorder), 0);
  lv_obj_set_style_border_width(box, 3, 0);
  lv_obj_set_style_pad_left(box, 18, 0);
  lv_obj_set_style_pad_right(box, 6, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(box, 15, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  make_label(box, font_body(), kMuted, icon);

  lv_obj_t *ta = lv_textarea_create(box);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, max_len);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_obj_set_flex_grow(ta, 1);
  lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ta, 0, 0);
  lv_obj_set_style_border_width(ta, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_pad_all(ta, 0, 0);
  lv_obj_set_style_text_font(ta, font_body(), 0);
  lv_obj_set_style_text_color(ta, c(kText), 0);
  lv_obj_set_style_text_color(ta, c(kMuted), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_border_color(ta, c(kCpu), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_CLICKED, nullptr);
  if (box_out) *box_out = box;
  return ta;
}

lv_obj_t *primary_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb) {
  lv_obj_t *ok = lv_btn_create(parent);
  lv_obj_remove_style_all(ok);
  lv_obj_set_size(ok, 168, 63);
  lv_obj_set_style_radius(ok, 18, 0);
  lv_obj_set_style_bg_color(ok, c(kCpu), 0);
  lv_obj_set_style_bg_color(ok, c(0x3FB3C4), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, 0);
  lv_obj_t *l = make_label(ok, font_body(), kBg, text);
  lv_obj_center(l);
  lv_obj_add_event_cb(ok, cb, LV_EVENT_CLICKED, nullptr);
  return ok;
}

void show_modal_error(const char *msg) {
  if (s_modal_err) {
    lv_label_set_text(s_modal_err, msg);
    lv_obj_clear_flag(s_modal_err, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---- Wi-Fi connect ------------------------------------------------------------------------------
void connect_ok_cb(lv_event_t *) {
  char ssid[33];
  if (s_ta_ssid)
    strlcpy(ssid, lv_textarea_get_text(s_ta_ssid), sizeof(ssid));
  else
    strlcpy(ssid, s_pending_ssid, sizeof(ssid));
  const char *pass = s_ta_pass ? lv_textarea_get_text(s_ta_pass) : "";
  const size_t plen = strlen(pass);
  if (!ssid[0]) {
    show_modal_error(tr(Str::Ssid));
    return;
  }
  if (!s_pending_open && (plen < 8 || plen > 64)) {
    show_modal_error(tr(Str::PassLenError));
    return;
  }
  storage::NetConfig cfg;
  storage::load_net(cfg);  // keep the agent address
  strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
  strlcpy(cfg.pass, s_pending_open ? "" : pass, sizeof(cfg.pass));
  storage::save_net(cfg);
  memset(&cfg, 0, sizeof(cfg));  // don't leave the password on the stack longer than needed
  close_modal();
  net::reconfigure();
  if (app::state().settings.demo_mode) app::set_demo_mode(false);  // connecting needs the network
  update();
}

void eye_cb(lv_event_t *e) {
  if (!s_ta_pass) return;
  lv_obj_t *b = lv_event_get_target(e);
  const bool show = lv_obj_has_state(b, LV_STATE_CHECKED);
  lv_textarea_set_password_mode(s_ta_pass, !show);
  lv_label_set_text(lv_obj_get_child(b, 0), show ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void open_connect(const char *ssid, bool secure) {
  const bool hidden = ssid == nullptr;
  s_pending_open = !hidden && !secure;
  strlcpy(s_pending_ssid, hidden ? "" : ssid, sizeof(s_pending_ssid));
  lv_obj_t *col = modal_base(hidden ? tr(Str::OtherNetwork) : ssid,
                             s_pending_open ? tr(Str::OpenNetwork) : tr(Str::WifiTitle));

  if (hidden) s_ta_ssid = make_field(s_fields, LV_SYMBOL_WIFI, tr(Str::Ssid), 32, &s_box_ssid);
  if (!s_pending_open) {
    s_ta_pass = make_field(s_fields, PCD_SYM_LOCK, tr(Str::Password), 64, &s_box_pass);
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_obj_t *eye = lv_btn_create(s_box_pass);
    lv_obj_remove_style_all(eye);
    lv_obj_set_size(eye, 51, 51);
    lv_obj_set_style_radius(eye, 15, 0);
    lv_obj_set_style_bg_color(eye, c(kSurfaceHi), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(eye, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_t *el = make_label(eye, font_body(), kMuted, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(el);
    lv_obj_add_event_cb(eye, eye_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  }
  primary_button(s_fields, tr(Str::Connect), connect_ok_cb);
  if (app::state().settings.demo_mode) make_label(col, font_small(), kPower, tr(Str::DemoWillTurnOff));
  s_modal_err = make_label(col, font_small(), kCritical, "");
  lv_obj_add_flag(s_modal_err, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *first = s_ta_ssid ? s_ta_ssid : s_ta_pass;
  if (first) {
    lv_keyboard_set_textarea(s_kb, first);
    lv_obj_add_state(first, LV_STATE_FOCUSED);
    focus_box(first);
  } else {
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  }
  // Keyboard "OK" key behaves like Connect; the keyboard-hide key closes the sheet.
  lv_obj_add_event_cb(s_kb, [](lv_event_t *e) { connect_ok_cb(e); }, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_kb, [](lv_event_t *) { close_modal(); }, LV_EVENT_CANCEL, nullptr);
}

void network_cb(lv_event_t *e) {
  const intptr_t idx = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  if (idx < 0) {
    open_connect(nullptr, true);
  } else if (idx < net::kMaxScan) {
    open_connect(s_results[idx].ssid, s_results[idx].secure);
  }
}

void forget_cb(lv_event_t *) {
  storage::NetConfig cfg;
  storage::load_net(cfg);
  cfg.ssid[0] = '\0';
  memset(cfg.pass, 0, sizeof(cfg.pass));
  storage::save_net(cfg);
  net::reconfigure();
  update();
}

// The page always looks for networks while it is visible: a scan when it opens, then every
// kRescanMs. The current list stays on screen (spinner in the header) until new results arrive.
constexpr uint32_t kRescanMs = 15000;
bool s_scanning = false;
bool s_have_list = false;     // a scan has filled the list at least once
uint32_t s_scan_done_ms = 0;

void start_scan() {
  if (!s_list || s_scanning) return;
  s_scanning = true;
  net::request_scan();
  lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
  if (!s_have_list) {
    lv_obj_clean(s_list);
    make_label(s_list, font_small(), kMuted, tr(Str::Scanning));
  }
}

void scan_cb(lv_event_t *) { start_scan(); }

uint32_t rssi_color(int rssi) {
  if (rssi >= -60) return kOk;
  if (rssi >= -72) return kPower;
  return kCritical;
}

void add_network_row(int idx, const net::ScanEntry *e) {
  lv_obj_t *b = lv_obj_create(s_list);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, LV_PCT(100), kTouchMin);
  lv_obj_set_style_bg_color(b, c(kSurfaceHi), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(b, c(kBorder), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_pad_hor(b, 6, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(b, network_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(idx)));
  if (!e) {
    lv_obj_t *l = make_label(b, font_body(), kCpu, tr(Str::OtherNetwork));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return;
  }
  lv_obj_t *icon = make_label(b, font_body(), rssi_color(e->rssi), LV_SYMBOL_WIFI);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *name = make_label(b, font_body(), kText, e->ssid);
  lv_obj_set_size(name, 400, lv_font_get_line_height(font_body()));
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 42, 0);
  char right[24];
  snprintf(right, sizeof(right), "%d %s", e->rssi, e->secure ? PCD_SYM_LOCK : "");
  lv_obj_t *r = make_label(b, font_small(), kMuted, right);
  lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
}

}  // namespace

void dev_open_connect(const char *ssid) { open_connect(ssid, true); }

lv_obj_t *create(lv_obj_t *parent) {
  // Landscape: [status + agent + forget] [networks list]. Back and title live in the top bar.
  lv_obj_t *page = make_page(parent, false);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(page, kGap, 0);

  lv_obj_t *left = lv_obj_create(page);
  lv_obj_remove_style_all(left);
  lv_obj_set_size(left, 264, LV_PCT(100));
  lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(left, kGap, 0);
  lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *card = make_card(left);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 9, 0);
  s_status = make_label(card, font_small(), kText, "");
  lv_obj_set_width(s_status, LV_PCT(100));
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  s_agent = make_label(card, font_small(), kMuted, "");
  lv_obj_set_width(s_agent, LV_PCT(100));
  lv_label_set_long_mode(s_agent, LV_LABEL_LONG_WRAP);
  lv_obj_t *edit = make_button(card, "");
  lv_label_set_text_fmt(lv_obj_get_child(edit, 0), "%s %s", LV_SYMBOL_SHUFFLE, tr(Str::PairButton));
  lv_obj_set_size(edit, LV_PCT(100), 51);
  lv_obj_set_style_text_font(lv_obj_get_child(edit, 0), font_small(), 0);
  lv_obj_add_event_cb(edit, [](lv_event_t *) { navigate(Page::Pair); }, LV_EVENT_CLICKED, nullptr);

  s_forget = make_button(left, "");
  lv_label_set_text_fmt(lv_obj_get_child(s_forget, 0), "%s %s", LV_SYMBOL_TRASH, tr(Str::Forget));
  lv_obj_set_size(s_forget, LV_PCT(100), 54);
  lv_obj_set_style_text_font(lv_obj_get_child(s_forget, 0), font_small(), 0);
  lv_obj_set_style_border_color(s_forget, c(kCritical), 0);
  lv_obj_add_event_cb(s_forget, forget_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *right = lv_obj_create(page);
  lv_obj_remove_style_all(right);
  lv_obj_set_height(right, LV_PCT(100));
  lv_obj_set_flex_grow(right, 1);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(right, 6, 0);
  lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

  // Networks header
  lv_obj_t *nh = lv_obj_create(right);
  lv_obj_remove_style_all(nh);
  lv_obj_set_size(nh, LV_PCT(100), 48);
  lv_obj_clear_flag(nh, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *nl = make_label(nh, font_small(), kMuted, tr(Str::Networks));
  lv_obj_align(nl, LV_ALIGN_LEFT_MID, 3, 0);
  s_spinner = lv_spinner_create(nh, 1000, 60);
  lv_obj_set_size(s_spinner, 30, 30);
  lv_obj_set_style_arc_width(s_spinner, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_spinner, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_spinner, c(kCpu), LV_PART_INDICATOR);
  lv_obj_align(s_spinner, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  s_scan_btn = make_button(nh, "");
  lv_label_set_text_fmt(lv_obj_get_child(s_scan_btn, 0), "%s %s", LV_SYMBOL_REFRESH, tr(Str::Scan));
  lv_obj_set_size(s_scan_btn, LV_SIZE_CONTENT, 45);
  lv_obj_set_style_pad_hor(s_scan_btn, 15, 0);
  lv_obj_set_style_text_font(lv_obj_get_child(s_scan_btn, 0), font_small(), 0);
  lv_obj_align(s_scan_btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(s_scan_btn, scan_cb, LV_EVENT_CLICKED, nullptr);

  // Network list (scrolls)
  s_list = make_card(right);
  lv_obj_set_width(s_list, LV_PCT(100));
  lv_obj_set_flex_grow(s_list, 1);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_list, 6, 0);
  lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  add_network_row(-1, nullptr);
  s_scanning = false;
  s_have_list = false;
  s_scan_done_ms = 0;

  update();
  return page;
}

void update() {
  if (!s_status) return;
  storage::NetConfig cfg;
  storage::load_net(cfg);
  const net::Status ns = net::status();
  const auto &st = app::state();
  char buf[160];
  if (!cfg.has_wifi()) {
    snprintf(buf, sizeof(buf), "%s", tr(Str::NotConnected));
    set_color(s_status, kMuted);
  } else if (ns.wifi_connected) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s\n%s\n%s · %d dBm", tr(Str::ConnectedTo), cfg.ssid, ns.ip, ns.rssi);
    set_color(s_status, kOk);
  } else {
    const net::Failure f = net::classify(ns.last_disconnect_reason);
    const char *why = f == net::Failure::WrongPassword ? tr(Str::WrongPassword)
                      : f == net::Failure::NotFound    ? tr(Str::NetNotFound)
                      : f == net::Failure::Other       ? tr(Str::ConnFailed)
                                                       : "";
    if (st.settings.demo_mode)
      snprintf(buf, sizeof(buf), "%s: %s\n%s", tr(Str::DemoMode), cfg.ssid, tr(Str::DemoWillTurnOff));
    else
      snprintf(buf, sizeof(buf), "%s %s…%s%s", tr(Str::ConnectingTo), cfg.ssid, why[0] ? "\n" : "", why);
    set_color(s_status, why[0] ? kWarning : kMuted);
  }
  set_text(s_status, buf);

  uint32_t agent_color = kMuted;
  if (cfg.has_agent()) {
    const bool unreachable = ns.wifi_connected && !ns.ws_connected && !st.settings.demo_mode;
    // "✓ MyPC" over "192.168.1.20:8765": fits the narrow column without awkward wraps.
    snprintf(buf, sizeof(buf), "%s%s\n%s:%u%s%s", ns.ws_connected ? LV_SYMBOL_OK " " : "",
             cfg.agent_name[0] ? cfg.agent_name : tr(Str::Agent), cfg.host, cfg.port, unreachable ? "\n" : "",
             unreachable ? tr(Str::AgentNoResponse) : "");
    agent_color = ns.ws_connected ? kOk : unreachable ? kWarning : kMuted;
  } else {
    snprintf(buf, sizeof(buf), "%s", tr(Str::AgentNotSet));
  }
  set_text(s_agent, buf);
  set_color(s_agent, agent_color);
  const bool has_wifi = cfg.has_wifi();
  memset(&cfg, 0, sizeof(cfg));

  if (has_wifi)
    lv_obj_clear_state(s_forget, LV_STATE_DISABLED);
  else
    lv_obj_add_state(s_forget, LV_STATE_DISABLED);
}

void on_show() {
  s_scan_done_ms = 0;
  start_scan();
}

void tick() {
  if (!s_list || s_scanning || s_modal) return;  // no rescans while the password sheet is open
  if (lv_tick_get() - s_scan_done_ms < kRescanMs) return;
  // Don't interrupt a connection attempt with periodic rescans (opening the page still scans).
  storage::NetConfig cfg;
  storage::load_net(cfg);
  const bool connecting = cfg.has_wifi() && !net::status().wifi_connected && !app::state().settings.demo_mode;
  memset(&cfg, 0, sizeof(cfg));
  if (!connecting) start_scan();
}

void on_scan() {
  if (!s_list) return;
  s_scanning = false;
  s_scan_done_ms = lv_tick_get();
  lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
  const int n = net::scan_results(s_results, net::kMaxScan);
  if (n < 0 && s_have_list) return;  // keep the last good list; the next rescan will try again
  lv_obj_clean(s_list);
  s_have_list = n > 0;
  if (n < 0) make_label(s_list, font_small(), kCritical, tr(Str::ScanFailed));
  if (n == 0) make_label(s_list, font_small(), kMuted, tr(Str::NoNetworks));
  for (int i = 0; i < n; i++) add_network_row(i, &s_results[i]);
  add_network_row(-1, nullptr);
}

}  // namespace wifi
}  // namespace ui
