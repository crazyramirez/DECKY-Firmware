// "Pair PC": PCs found over mDNS (Bonjour), manual IP fallback, and a big numeric keypad for the
// one-time 6-digit code shown in the PC app. The code is sent with pair.request; the agent answers
// with a device token that the network task stores in NVS.

#include <Arduino.h>  // IPAddress
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "network/net_task.h"
#include "storage/net_store.h"
#include "ui/ui_internal.h"

namespace ui {
namespace pair {
namespace {

using namespace theme;

lv_obj_t *s_status = nullptr;
lv_obj_t *s_steps = nullptr;
lv_obj_t *s_forget = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_spinner = nullptr;
lv_obj_t *s_search = nullptr;
lv_obj_t *s_nowifi = nullptr;

net::PcEntry s_pcs[net::kMaxPcs];
net::PcEntry s_target;

// ---- Modals (top layer): 6-digit code keypad and manual IP entry ---------------------------------
lv_obj_t *s_modal = nullptr;
lv_obj_t *s_msg = nullptr;
lv_obj_t *s_pad = nullptr;
lv_obj_t *s_cell[6] = {};
lv_obj_t *s_cell_lbl[6] = {};
lv_obj_t *s_field[2] = {};  // manual entry: host, port
lv_obj_t *s_field_val[2] = {};
char s_code[7] = "";
char s_host[16] = "";
char s_port[6] = "";
int s_active = 0;  // manual entry: field being edited
bool s_busy = false;

constexpr uint16_t kOkBtn = 11;  // index of the OK key in the code map
const char *s_code_map[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                            LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};
const char *s_ip_map[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                          ".", "0", LV_SYMBOL_BACKSPACE, ""};

void close_modal() {
  if (s_modal) {
    lv_obj_del_async(s_modal);
    s_modal = s_msg = s_pad = nullptr;
    for (auto &o : s_cell) o = nullptr;
    for (auto &o : s_cell_lbl) o = nullptr;
    s_field[0] = s_field[1] = s_field_val[0] = s_field_val[1] = nullptr;
  }
  s_busy = false;
}

void set_msg(const char *text, uint32_t color) {
  if (!s_msg) return;
  lv_label_set_text(s_msg, text);
  lv_obj_set_style_text_color(s_msg, c(color), 0);
  if (text[0])  // an empty message takes no room in the column
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
}

// Header: round close button, title and a muted subtitle.
lv_obj_t *modal_base(const char *title, const char *subtitle) {
  close_modal();
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_modal);
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_modal, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_modal, kMargin, 0);
  lv_obj_set_style_pad_row(s_modal, 10, 0);
  lv_obj_set_flex_flow(s_modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *head = lv_obj_create(s_modal);
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
  lv_obj_add_event_cb(x, [](lv_event_t *) { close_modal(); }, LV_EVENT_CLICKED, nullptr);
  const bool has_sub = subtitle && subtitle[0];
  lv_obj_t *t = make_label(head, font_title(), kText, title);
  lv_obj_set_width(t, 250);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 54, has_sub ? -1 : 10);
  if (has_sub) {
    lv_obj_t *st = make_label(head, font_small(), kMuted, subtitle);
    lv_obj_set_width(st, 250);
    lv_label_set_long_mode(st, LV_LABEL_LONG_DOT);
    lv_obj_align(st, LV_ALIGN_BOTTOM_LEFT, 54, 1);
  }
  return s_modal;
}

// Keypad keys: the OK key is the accent button (dimmed while disabled); backspace is borderless.
void pad_draw_cb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *d = lv_event_get_draw_part_dsc(e);
  if (d->class_p != &lv_btnmatrix_class || d->type != LV_BTNMATRIX_DRAW_PART_BTN) return;
  lv_obj_t *pad = lv_event_get_target(e);
  const char *txt = lv_btnmatrix_get_btn_text(pad, d->id);
  if (!txt) return;
  const bool pressed = lv_btnmatrix_get_selected_btn(pad) == d->id && lv_obj_has_state(pad, LV_STATE_PRESSED);
  if (!strcmp(txt, LV_SYMBOL_OK)) {
    const bool off = lv_btnmatrix_has_btn_ctrl(pad, d->id, LV_BTNMATRIX_CTRL_DISABLED);
    d->rect_dsc->bg_color = c(off ? kSurface : pressed ? 0x3FB3C4 : kCpu);
    d->rect_dsc->border_width = off ? 1 : 0;
    d->label_dsc->color = c(off ? kBorder : kBg);
  } else if (!strcmp(txt, LV_SYMBOL_BACKSPACE)) {
    d->rect_dsc->bg_opa = pressed ? LV_OPA_COVER : LV_OPA_TRANSP;
    d->rect_dsc->border_width = 0;
    d->label_dsc->color = c(kMuted);
  }
}

lv_obj_t *make_pad(lv_obj_t *parent, const char **map) {
  lv_obj_t *pad = lv_btnmatrix_create(parent);
  lv_btnmatrix_set_map(pad, map);
  lv_obj_set_width(pad, LV_PCT(100));
  lv_obj_set_flex_grow(pad, 1);
  lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pad, 0, 0);
  lv_obj_set_style_pad_all(pad, 0, 0);
  lv_obj_set_style_pad_gap(pad, 8, 0);
  lv_obj_set_style_bg_color(pad, c(kSurface), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(pad, c(kSurfaceHi), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(pad, c(kBorder), LV_PART_ITEMS);
  lv_obj_set_style_border_width(pad, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_opa(pad, LV_OPA_50, LV_PART_ITEMS);
  lv_obj_set_style_text_color(pad, c(kText), LV_PART_ITEMS);
  lv_obj_set_style_text_font(pad, font_value(), LV_PART_ITEMS);
  lv_obj_set_style_radius(pad, 14, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(pad, 0, LV_PART_ITEMS);
  lv_obj_set_style_opa(pad, LV_OPA_50, LV_STATE_DISABLED);
  lv_obj_add_event_cb(pad, pad_draw_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);
  return pad;
}

// ---- code keypad -----------------------------------------------------------------------------------
void render_code() {
  const int n = static_cast<int>(strlen(s_code));
  for (int i = 0; i < 6; i++) {
    if (!s_cell[i]) return;
    const char d[2] = {i < n ? s_code[i] : '\0', '\0'};
    lv_label_set_text(s_cell_lbl[i], d);
    const bool current = n == i && !s_busy;
    lv_obj_set_style_border_color(s_cell[i], c(current ? kCpu : kBorder), 0);
    lv_obj_set_style_border_width(s_cell[i], current ? 2 : 1, 0);
    lv_obj_set_style_bg_color(s_cell[i], c(i < n ? kSurfaceHi : kSurface), 0);
  }
  if (s_pad) {
    if (n == 6)
      lv_btnmatrix_clear_btn_ctrl(s_pad, kOkBtn, LV_BTNMATRIX_CTRL_DISABLED);
    else
      lv_btnmatrix_set_btn_ctrl(s_pad, kOkBtn, LV_BTNMATRIX_CTRL_DISABLED);
  }
}

void submit() {
  if (strlen(s_code) != 6 || s_busy) return;
  s_busy = true;
  set_msg(tr(Str::PairInProgress), kCpu);
  render_code();
  lv_obj_add_state(s_pad, LV_STATE_DISABLED);
  if (app::state().settings.demo_mode) app::set_demo_mode(false);  // pairing needs the network
  net::start_pairing(s_target, s_code);
}

void code_pad_cb(lv_event_t *e) {
  if (s_busy) return;
  lv_obj_t *pad = lv_event_get_target(e);
  const char *txt = lv_btnmatrix_get_btn_text(pad, lv_btnmatrix_get_selected_btn(pad));
  if (!txt) return;
  const size_t n = strlen(s_code);
  if (!strcmp(txt, LV_SYMBOL_BACKSPACE)) {
    if (n) s_code[n - 1] = '\0';
  } else if (!strcmp(txt, LV_SYMBOL_OK)) {
    submit();
    return;
  } else if (n < 6 && txt[0] >= '0' && txt[0] <= '9') {
    s_code[n] = txt[0];
    s_code[n + 1] = '\0';
  }
  render_code();
  set_msg(tr(Str::PairCodeHint), kMuted);
  if (strlen(s_code) == 6) submit();  // auto-submit on the 6th digit
}

void open_code(const net::PcEntry &pc) {
  s_target = pc;
  s_code[0] = '\0';
  char sub[64];
  if (pc.name[0] && strcmp(pc.name, pc.host) != 0)
    snprintf(sub, sizeof(sub), "%s · %s", pc.name, pc.host);
  else
    snprintf(sub, sizeof(sub), "%s", pc.host);
  lv_obj_t *m = modal_base(tr(Str::PairEnterCode), sub);

  lv_obj_t *cells = lv_obj_create(m);
  lv_obj_remove_style_all(cells);
  lv_obj_set_size(cells, LV_PCT(100), 60);
  lv_obj_set_flex_flow(cells, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cells, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_clear_flag(cells, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < 6; i++) {
    s_cell[i] = lv_obj_create(cells);
    lv_obj_remove_style_all(s_cell[i]);
    lv_obj_set_size(s_cell[i], 44, 54);
    lv_obj_set_style_radius(s_cell[i], 12, 0);
    lv_obj_set_style_bg_opa(s_cell[i], LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_cell[i], LV_OBJ_FLAG_SCROLLABLE);
    s_cell_lbl[i] = make_label(s_cell[i], font_value(), kText, "");
    lv_obj_center(s_cell_lbl[i]);
  }
  s_msg = make_label(m, font_small(), kMuted, tr(Str::PairCodeHint));
  lv_obj_set_width(s_msg, LV_PCT(100));
  lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);

  s_pad = make_pad(m, s_code_map);
  lv_obj_add_event_cb(s_pad, code_pad_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  render_code();
}

// ---- manual IP entry -------------------------------------------------------------------------------
void render_fields() {
  for (int i = 0; i < 2; i++) {
    if (!s_field[i]) return;
    const bool on = i == s_active;
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%s", i == 0 ? s_host : s_port, on ? "|" : "");
    lv_label_set_text(s_field_val[i], buf);
    lv_obj_set_style_border_color(s_field[i], c(on ? kCpu : kBorder), 0);
    lv_obj_set_style_border_width(s_field[i], on ? 2 : 1, 0);
  }
}

void field_cb(lv_event_t *e) {
  s_active = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  render_fields();
}

lv_obj_t *make_field(lv_obj_t *parent, const char *caption, int idx) {
  lv_obj_t *f = lv_obj_create(parent);
  lv_obj_remove_style_all(f);
  lv_obj_set_height(f, 60);
  lv_obj_set_style_radius(f, 12, 0);
  lv_obj_set_style_bg_color(f, c(kSurface), 0);
  lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(f, 12, 0);
  lv_obj_set_style_pad_ver(f, 7, 0);
  lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(f, field_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(idx)));
  lv_obj_t *cap = make_label(f, font_small(), kMuted, caption);
  lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
  s_field_val[idx] = make_label(f, font_title(), kText, "");
  lv_obj_align(s_field_val[idx], LV_ALIGN_BOTTOM_LEFT, 0, 2);
  s_field[idx] = f;
  return f;
}

void manual_ok_cb(lv_event_t *) {
  IPAddress probe;
  const long port = strtol(s_port, nullptr, 10);
  if (!probe.fromString(s_host) || port < 1 || port > 65535) {
    set_msg(tr(Str::HostError), kCritical);
    return;
  }
  net::PcEntry pc;
  strlcpy(pc.host, s_host, sizeof(pc.host));
  strlcpy(pc.name, s_host, sizeof(pc.name));
  pc.port = static_cast<uint16_t>(port);
  open_code(pc);  // replaces this modal
}

void ip_pad_cb(lv_event_t *e) {
  lv_obj_t *pad = lv_event_get_target(e);
  const char *txt = lv_btnmatrix_get_btn_text(pad, lv_btnmatrix_get_selected_btn(pad));
  if (!txt) return;
  char *buf = s_active == 0 ? s_host : s_port;
  const size_t cap = s_active == 0 ? sizeof(s_host) : sizeof(s_port);
  const size_t n = strlen(buf);
  if (!strcmp(txt, LV_SYMBOL_BACKSPACE)) {
    if (n) buf[n - 1] = '\0';
  } else if ((txt[0] >= '0' && txt[0] <= '9') || (txt[0] == '.' && s_active == 0)) {
    if (n + 1 < cap) {
      buf[n] = txt[0];
      buf[n + 1] = '\0';
    }
  }
  set_msg("", kMuted);
  render_fields();
}

void open_manual(lv_event_t *) {
  storage::NetConfig cfg;
  storage::load_net(cfg);
  strlcpy(s_host, cfg.host, sizeof(s_host));
  snprintf(s_port, sizeof(s_port), "%u", cfg.port ? cfg.port : 8765);
  memset(&cfg, 0, sizeof(cfg));
  s_active = 0;
  lv_obj_t *m = modal_base(tr(Str::PairManual), tr(Str::AgentHost));

  lv_obj_t *row = lv_obj_create(m);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_grow(make_field(row, "IP", 0), 1);
  lv_obj_set_width(make_field(row, tr(Str::Port), 1), 92);

  s_msg = make_label(m, font_small(), kCritical, "");
  lv_obj_set_width(s_msg, LV_PCT(100));
  lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *ok = lv_btn_create(m);
  lv_obj_remove_style_all(ok);
  lv_obj_set_size(ok, LV_PCT(100), 46);
  lv_obj_set_style_radius(ok, 12, 0);
  lv_obj_set_style_bg_color(ok, c(kCpu), 0);
  lv_obj_set_style_bg_color(ok, c(0x3FB3C4), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, 0);
  lv_obj_t *okl = make_label(ok, font_body(), kBg, tr(Str::Connect));
  lv_obj_center(okl);
  lv_obj_add_event_cb(ok, manual_ok_cb, LV_EVENT_CLICKED, nullptr);

  s_pad = make_pad(m, s_ip_map);
  lv_obj_add_event_cb(s_pad, ip_pad_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  render_fields();
}

void pc_cb(lv_event_t *e) {
  const intptr_t i = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  if (i >= 0 && i < net::kMaxPcs) open_code(s_pcs[i]);
}

void search_cb(lv_event_t *) {
  net::request_discovery();
  lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_state(s_search, LV_STATE_DISABLED);
  lv_obj_clean(s_list);
  make_label(s_list, font_small(), kMuted, tr(Str::PairSearching));
}

void forget_cb(lv_event_t *) {
  net::forget_pc();
  update();
}

lv_obj_t *row_button(lv_obj_t *parent, const char *left, const char *right, uint32_t left_color) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, LV_PCT(100), kTouchMin + 4);
  lv_obj_set_style_bg_color(b, c(kSurfaceHi), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(b, c(kBorder), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_pad_hor(b, 4, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *l = make_label(b, font_body(), left_color, left);
  const bool wide_right = right && strlen(right) > 4;  // an IP needs room; a chevron does not
  lv_obj_set_width(l, wide_right ? 170 : 250);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  if (right && right[0]) {
    lv_obj_t *r = make_label(b, font_small(), kMuted, right);
    lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  return b;
}

void add_manual_row() {
  lv_obj_t *b = row_button(s_list, tr(Str::PairManual), LV_SYMBOL_RIGHT, kCpu);
  lv_obj_add_event_cb(b, open_manual, LV_EVENT_CLICKED, nullptr);
}

}  // namespace

void open_code_sheet(const net::PcEntry &pc) { open_code(pc); }
void open_manual_sheet() { open_manual(nullptr); }

void dev_open_code() {
  net::PcEntry pc;
  strlcpy(pc.name, "MyPC", sizeof(pc.name));
  strlcpy(pc.host, "192.168.1.20", sizeof(pc.host));
  pc.port = 8765;
  open_code(pc);
}
void dev_open_manual() { open_manual(nullptr); }

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
  lv_obj_add_event_cb(back, [](lv_event_t *) { navigate(Page::Settings); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *title = make_label(head, font_title(), kCpu, tr(Str::PairTitle));
  lv_obj_align(title, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t *card = make_card(page);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 6, 0);
  s_status = make_label(card, font_small(), kText, "");
  lv_obj_set_width(s_status, LV_PCT(100));
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  s_steps = make_label(card, font_small(), kMuted, tr(Str::PairSteps));
  lv_obj_set_width(s_steps, LV_PCT(100));
  lv_label_set_long_mode(s_steps, LV_LABEL_LONG_WRAP);

  s_nowifi = make_button(page, "");
  lv_label_set_text_fmt(lv_obj_get_child(s_nowifi, 0), "%s  %s", LV_SYMBOL_WIFI, tr(Str::PairWifiButton));
  lv_obj_set_width(s_nowifi, LV_PCT(100));
  lv_obj_set_style_border_color(s_nowifi, c(kWarning), 0);
  lv_obj_add_event_cb(s_nowifi, [](lv_event_t *) { navigate(Page::Wifi); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lh = lv_obj_create(page);
  lv_obj_remove_style_all(lh);
  lv_obj_set_size(lh, LV_PCT(100), 38);
  lv_obj_clear_flag(lh, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *ll = make_label(lh, font_small(), kMuted, tr(Str::PairFound));
  lv_obj_align(ll, LV_ALIGN_LEFT_MID, 0, 0);
  s_spinner = lv_spinner_create(lh, 1000, 60);
  lv_obj_set_size(s_spinner, 22, 22);
  lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_spinner, c(kCpu), LV_PART_INDICATOR);
  lv_obj_align(s_spinner, LV_ALIGN_CENTER, 10, 0);
  lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  s_search = make_button(lh, "");
  lv_label_set_text_fmt(lv_obj_get_child(s_search, 0), "%s %s", LV_SYMBOL_REFRESH, tr(Str::PairSearchAgain));
  lv_obj_set_size(s_search, LV_SIZE_CONTENT, 36);
  lv_obj_set_style_pad_hor(s_search, 10, 0);
  lv_obj_set_style_text_font(lv_obj_get_child(s_search, 0), font_small(), 0);
  lv_obj_align(s_search, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(s_search, search_cb, LV_EVENT_CLICKED, nullptr);

  s_list = make_card(page);
  lv_obj_set_width(s_list, LV_PCT(100));
  lv_obj_set_flex_grow(s_list, 1);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_list, 6, 0);
  lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  add_manual_row();

  s_forget = make_button(page, "");
  lv_label_set_text_fmt(lv_obj_get_child(s_forget, 0), "%s %s", LV_SYMBOL_TRASH, tr(Str::PairForget));
  lv_obj_set_width(s_forget, LV_PCT(100));
  lv_obj_set_height(s_forget, 40);
  lv_obj_set_style_border_color(s_forget, c(kCritical), 0);
  lv_obj_add_event_cb(s_forget, forget_cb, LV_EVENT_CLICKED, nullptr);

  update();
  return page;
}

void update() {
  if (!s_status) return;
  const net::Status ns = net::status();
  const auto &st = app::state();
  char buf[128];
  if (ns.paired && ns.agent_name[0]) {
    snprintf(buf, sizeof(buf), "%s %s %s", tr(Str::PairPairedWith), ns.agent_name,
             st.link == app::LinkState::Online && !st.settings.demo_mode ? LV_SYMBOL_OK : "");
    set_color(s_status, kOk);
  } else if (st.link == app::LinkState::Unauthorized) {
    snprintf(buf, sizeof(buf), "%s", tr(Str::StateUnauthorized));
    set_color(s_status, kWarning);
  } else {
    snprintf(buf, sizeof(buf), "%s", tr(Str::PairNotPaired));
    set_color(s_status, kMuted);
  }
  set_text(s_status, buf);
  if (ns.paired) {
    lv_obj_clear_flag(s_forget, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_steps, LV_OBJ_FLAG_HIDDEN);  // already paired: give the list the space
  } else {
    lv_obj_add_flag(s_forget, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_steps, LV_OBJ_FLAG_HIDDEN);
  }
  if (ns.wifi_connected)
    lv_obj_add_flag(s_nowifi, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_clear_flag(s_nowifi, LV_OBJ_FLAG_HIDDEN);
}

void on_show() {
  // Search automatically every time the page opens: zero taps to see the PCs.
  if (net::status().wifi_connected) search_cb(nullptr);
  update();
}

void on_discovery() {
  if (!s_list) return;
  lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_state(s_search, LV_STATE_DISABLED);
  lv_obj_clean(s_list);
  const int n = net::discovery_results(s_pcs, net::kMaxPcs);
  if (n == net::kDiscoveryNoWifi) {
    make_label(s_list, font_small(), kWarning, tr(Str::PairNoWifi));
  } else if (n <= 0) {
    lv_obj_t *l = make_label(s_list, font_small(), kMuted, tr(Str::PairNoneFound));
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }
  for (int i = 0; i < n; i++) {
    lv_obj_t *b = row_button(s_list, s_pcs[i].name[0] ? s_pcs[i].name : s_pcs[i].host, s_pcs[i].host, kText);
    lv_obj_add_event_cb(b, pc_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
  add_manual_row();
  update();
}

void on_pair_done(const app::PairOutcome &r) {
  if (!s_modal || !s_msg) {
    update();
    return;
  }
  if (r.ok) {
    set_msg(tr(Str::PairOk), kOk);
    close_modal();
    navigate(Page::Home);
    return;
  }
  s_busy = false;
  s_code[0] = '\0';
  render_code();
  if (s_pad) lv_obj_clear_state(s_pad, LV_STATE_DISABLED);
  const char *msg = !strcmp(r.error, "invalid_code") ? tr(Str::PairErrCode)
                    : !strcmp(r.error, "timeout")    ? tr(Str::PairErrTimeout)
                    : !strcmp(r.error, "no_wifi")    ? tr(Str::PairNoWifi)
                                                     : tr(Str::PairErrUnreachable);
  set_msg(msg, kCritical);
  update();
}

}  // namespace pair
}  // namespace ui
