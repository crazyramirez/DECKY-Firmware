// Root layout (landscape 480x272):
//   ┌──────┬──────────────────────────────────────────┐
//   │ rail │ top bar: [‹] title · subtitle   [bat][pill]│
//   │ 64px ├──────────────────────────────────────────┤
//   │ tabs │ page content (416 x 242)                 │
//   └──────┴──────────────────────────────────────────┘
// The navigation rail holds the user-ordered tabs (Settings > Tabs) and Settings at the bottom.
// Sub-pages (CPU/GPU detail, Wi-Fi, Pair PC, coin detail) show a back button in the top bar
// instead of their own header row. Pages are created once and hidden/shown, so navigation never
// reallocates widgets.

#include <Arduino.h>
#include <stdio.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "board/battery.h"
#include "ui/status_tap.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace {

using namespace theme;

struct NavItem {
  lv_obj_t *btn = nullptr;
  lv_obj_t *icon = nullptr;
  lv_obj_t *text = nullptr;
  lv_obj_t *marker = nullptr;
  Page page = Page::Home;
};

lv_obj_t *s_root = nullptr;
lv_obj_t *s_back = nullptr;
lv_obj_t *s_title = nullptr;
lv_obj_t *s_sub = nullptr;
lv_obj_t *s_pill = nullptr;
lv_obj_t *s_pill_dot = nullptr;
lv_obj_t *s_pill_lbl = nullptr;
lv_obj_t *s_deck_bat = nullptr;  // deck USB indicator left of the pill
lv_obj_t *s_content = nullptr;
lv_obj_t *s_pages[kPageCount] = {};
constexpr int kNavCount = storage::kTabCount + 1;  // + Settings
NavItem s_nav[kNavCount];
Page s_page = Page::Home;
bool s_first_build = true;
bool s_sub_custom = false;  // the page set its own subtitle (otherwise: PC name on monitor pages)
uint32_t s_metrics_ms = 0;
bool s_metrics_force = true;  // next telemetry repaints the metrics pages regardless of the period
uint32_t s_warm_until = 0;    // after boot / (re)connect: repaint on every snapshot until then
bool s_was_live = false;
constexpr uint32_t kWarmMs = 6000;
lv_obj_t *s_alert = nullptr;
lv_obj_t *s_alert_lbl = nullptr;

// Status LED colours: off = a dim grey with a lighter rim (lit ones glow in their own colour).
constexpr uint32_t kLedOff = 0x3F4757;
constexpr uint32_t kLedOffRim = 0x667085;

// Where the back button of a sub-page goes (Home = no back button).
Page parent_of(Page p) {
  switch (p) {
    case Page::DetailCpu:
    case Page::DetailGpu: return Page::Home;
    case Page::Wifi:
    case Page::Pair: return Page::Settings;
    case Page::CryptoDetail: return Page::Crypto;
    default: return p;
  }
}

bool shows_pc_name(Page p) {
  return p == Page::Home || p == Page::Thermals || p == Page::Controls || p == Page::DetailCpu || p == Page::DetailGpu;
}

// The PC went away while one of its pages was open: we moved to s_auto_to and go back to s_auto_from
// when it returns, unless the user picked another page meanwhile.
bool s_auto_moved = false;
Page s_auto_from = Page::Home, s_auto_to = Page::Home;

bool is_pc_page(Page p) {
  return p == Page::Home || p == Page::Thermals || p == Page::Controls || p == Page::DetailCpu || p == Page::DetailGpu;
}

// First tab of the rail that is shown (the user's order, hidden tabs and the PC tabs without a PC skipped).
Page first_tab_page() {
  const auto &cfg = app::state().settings;
  for (int i = 0; i < storage::kTabCount; i++)
    if (!(app::hidden_tabs() & (1u << cfg.tab_order[i]))) return tab_page(cfg.tab_order[i]);
  return Page::Settings;  // every tab hidden: Settings is the only one left
}

void nav_cb(lv_event_t *e) {
  s_auto_moved = false;
  const auto p = static_cast<Page>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  navigate(p);
}

void alert_cb(lv_event_t *) { lv_obj_add_flag(s_alert, LV_OBJ_FLAG_HIDDEN); }

void build_top_bar(lv_obj_t *parent) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, LV_PCT(100), kTopBarH);
  lv_obj_set_style_bg_color(bar, c(kBg), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(bar, c(kBorder), 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_pad_left(bar, 6, 0);
  lv_obj_set_style_pad_right(bar, kMargin, 0);
  lv_obj_set_style_pad_column(bar, 6, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Back (sub-pages only): a generous hit area around a small chevron.
  s_back = lv_btn_create(bar);
  lv_obj_remove_style_all(s_back);
  lv_obj_set_size(s_back, 30, 26);
  lv_obj_set_style_radius(s_back, 8, 0);
  lv_obj_set_style_bg_color(s_back, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(s_back, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_back, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(s_back, 8);
  lv_obj_t *bl = make_label(s_back, font_small(), kText, LV_SYMBOL_LEFT);
  lv_obj_center(bl);
  lv_obj_add_event_cb(s_back, [](lv_event_t *) { navigate(parent_of(s_page)); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);

  s_title = make_label(bar, font_body(), kText, "");
  lv_obj_set_style_pad_left(s_title, 2, 0);
  s_sub = make_label(bar, font_small(), kMuted, "");
  lv_label_set_long_mode(s_sub, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(s_sub, 1);
  lv_obj_set_height(s_sub, lv_font_get_line_height(font_small()));

  // Right cluster, one tap target (status_tap.h): [deck power] [status pill]. A double tap starts the
  // screensaver when it is enabled.
  lv_obj_t *status = lv_obj_create(bar);
  lv_obj_remove_style_all(status);
  lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_column(status, 6, 0);  // as between the bar's own items
  lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_ext_click_area(status, 12);
  s_deck_bat = make_label(status, font_small(), kMuted, "");
  lv_obj_add_flag(s_deck_bat, LV_OBJ_FLAG_HIDDEN);

  // Status LED: green while the PC app is connected, grey otherwise — no words, demo mode aside (amber
  // + DEMO, so synthetic data never passes as real). Settings shows the details (Wi-Fi, PC name).
  // Tapping it while grey goes straight to the fix: Wi-Fi setup or pairing.
  s_pill = lv_obj_create(status);
  lv_obj_remove_style_all(s_pill);
  lv_obj_set_size(s_pill, LV_SIZE_CONTENT, 20);
  lv_obj_set_style_pad_hor(s_pill, 4, 0);
  lv_obj_set_style_pad_column(s_pill, 6, 0);
  lv_obj_set_flex_flow(s_pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  s_pill_dot = lv_obj_create(s_pill);
  lv_obj_remove_style_all(s_pill_dot);
  lv_obj_set_size(s_pill_dot, 10, 10);
  lv_obj_set_style_radius(s_pill_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_pill_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_pill_dot, c(kLedOff), 0);
  lv_obj_set_style_border_width(s_pill_dot, 1, 0);
  lv_obj_set_style_border_color(s_pill_dot, c(kLedOffRim), 0);
  lv_obj_set_style_shadow_width(s_pill_dot, 16, 0);
  lv_obj_set_style_shadow_spread(s_pill_dot, 1, 0);
  lv_obj_set_style_shadow_opa(s_pill_dot, LV_OPA_TRANSP, 0);
  s_pill_lbl = make_label(s_pill, &lv_font_montserrat_12, kPower, "");
  lv_obj_add_flag(s_pill_lbl, LV_OBJ_FLAG_HIDDEN);
  attach_status_tap(status, [] {
    const auto l = app::state().link;
    if (app::state().settings.demo_mode || l == app::LinkState::Online || l == app::LinkState::Stale) return;
    navigate(l == app::LinkState::NoWifi ? Page::Wifi : Page::Pair);
  });
}

void build_nav(lv_obj_t *parent) {
  lv_obj_t *nav = lv_obj_create(parent);
  lv_obj_remove_style_all(nav);
  lv_obj_set_size(nav, kNavW, LV_PCT(100));
  lv_obj_set_style_bg_color(nav, c(kSurface), 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_color(nav, c(kBorder), 0);
  lv_obj_set_style_border_width(nav, 1, 0);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

  // User-ordered tabs first (Settings > Tabs), Settings always last.
  const uint8_t *order = app::state().settings.tab_order;
  const char *icons[kNavCount];
  Str labels[kNavCount];
  Page pages[kNavCount];
  for (int i = 0; i < storage::kTabCount; i++) {
    icons[i] = tab_icon(order[i]);
    labels[i] = tab_label(order[i]);
    pages[i] = tab_page(order[i]);
  }
  icons[kNavCount - 1] = LV_SYMBOL_SETTINGS;
  labels[kNavCount - 1] = Str::NavSettings;
  pages[kNavCount - 1] = Page::Settings;
  for (int i = 0; i < kNavCount; i++) {
    NavItem &n = s_nav[i];
    n.page = pages[i];
    n.btn = lv_obj_create(nav);
    lv_obj_remove_style_all(n.btn);
    lv_obj_set_width(n.btn, LV_PCT(100));
    lv_obj_set_flex_grow(n.btn, 1);  // 6 items share 272 px (~45 px each); hidden tabs give theirs away
    lv_obj_add_flag(n.btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(n.btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(n.btn, c(kSurfaceHi), 0);
    lv_obj_set_style_bg_opa(n.btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(n.btn, c(kBorder), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(n.btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(n.btn, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(pages[i])));
    n.marker = lv_obj_create(n.btn);
    lv_obj_remove_style_all(n.marker);
    lv_obj_set_size(n.marker, 3, 26);
    lv_obj_set_style_radius(n.marker, 2, 0);
    lv_obj_set_style_bg_color(n.marker, c(kCpu), 0);
    lv_obj_set_style_bg_opa(n.marker, LV_OPA_COVER, 0);
    lv_obj_align(n.marker, LV_ALIGN_LEFT_MID, 0, 0);
    n.icon = make_label(n.btn, font_title(), kMuted, icons[i]);
    lv_obj_align(n.icon, LV_ALIGN_CENTER, 0, -6);
    n.text = make_label(n.btn, font_micro(), kMuted, tr(labels[i]));
    lv_obj_align(n.text, LV_ALIGN_CENTER, 0, 13);
  }
}

void build_alert_overlay() {
  s_alert = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_alert);
  lv_obj_set_size(s_alert, kContentW - 4 * kMargin, LV_SIZE_CONTENT);
  lv_obj_align(s_alert, LV_ALIGN_TOP_MID, kNavW / 2, kTopBarH + 6);
  lv_obj_set_style_radius(s_alert, kRadius, 0);
  lv_obj_set_style_bg_opa(s_alert, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_alert, 10, 0);
  lv_obj_add_flag(s_alert, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_alert, alert_cb, LV_EVENT_CLICKED, nullptr);
  s_alert_lbl = make_label(s_alert, font_body(), kBg, "");
  lv_obj_set_width(s_alert_lbl, LV_PCT(100));
  lv_label_set_long_mode(s_alert_lbl, LV_LABEL_LONG_WRAP);
}

void update_nav() {
  const Page p = s_page;
  const Page owner = parent_of(p);  // a sub-page lights up its tab
  for (int i = 0; i < kNavCount; i++) {
    const bool active = s_nav[i].page == owner;
    const uint32_t col = active ? kCpu : kMuted;
    set_color(s_nav[i].icon, col);
    set_color(s_nav[i].text, active ? kText : kMuted);
    lv_obj_set_style_bg_opa(s_nav[i].btn, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (active)
      lv_obj_clear_flag(s_nav[i].marker, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_nav[i].marker, LV_OBJ_FLAG_HIDDEN);
  }
}

void default_title(Page p) {
  switch (p) {
    case Page::Home: set_title(tr(Str::NavHome), kText); break;
    case Page::Thermals: set_title(tr(Str::ThermalsTitle), kText); break;
    case Page::Controls: set_title(tr(Str::NavControls), kText); break;
    case Page::Settings: set_title(tr(Str::SettingsTitle), kText); break;
    case Page::DetailCpu: set_title(tr(Str::Cpu), kCpu); break;
    case Page::DetailGpu: set_title(tr(Str::Gpu), kGpu); break;
    case Page::Wifi: set_title(tr(Str::WifiTitle), kText); break;
    case Page::Pair: set_title(tr(Str::PairTitle), kText); break;
    case Page::Today: set_title(tr(Str::NavToday), kText); break;
    case Page::Crypto: set_title(tr(Str::NavCrypto), kText); break;
    case Page::CryptoDetail: break;  // the page sets the coin name
  }
}

void refresh_subtitle() {
  const auto &st = app::state();
  if (st.settings.demo_mode) {
    set_text(s_sub, tr(Str::DemoBanner));
    set_color(s_sub, kPower);
    return;
  }
  set_color(s_sub, kMuted);
  if (s_sub_custom) return;
  if (shows_pc_name(s_page))
    set_text(s_sub, st.has_snapshot && st.snap.host[0] ? st.snap.host : st.settings.device_name);
  else
    set_text(s_sub, "");
}

// Deck power: charge bolt while a USB host is connected, battery level + % when measurable.
void update_deck_battery() {
  if (!s_deck_bat) return;
  const battery::State b = battery::state();
  if (b.source == battery::Source::Unknown) {
    if (!lv_obj_has_flag(s_deck_bat, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(s_deck_bat, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (lv_obj_has_flag(s_deck_bat, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(s_deck_bat, LV_OBJ_FLAG_HIDDEN);
  char buf[24];
  if (b.source == battery::Source::Usb) {
    snprintf(buf, sizeof(buf), "%s", LV_SYMBOL_CHARGE);
    set_color(s_deck_bat, kOk);
  } else {
    const int p = b.pct;
    const char *sym = p >= 85 ? LV_SYMBOL_BATTERY_FULL : p >= 60 ? LV_SYMBOL_BATTERY_3 : p >= 35 ? LV_SYMBOL_BATTERY_2
                      : p >= 12 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    snprintf(buf, sizeof(buf), "%s %d%%", sym, p);
    set_color(s_deck_bat, p > 20 ? kText : p > 10 ? kWarning : kCritical);
  }
  set_text(s_deck_bat, buf);
}

}  // namespace

Page tab_page(uint8_t tab) {
  switch (static_cast<storage::Tab>(tab)) {
    case storage::Tab::Thermals: return Page::Thermals;
    case storage::Tab::Player: return Page::Controls;
    case storage::Tab::Today: return Page::Today;
    case storage::Tab::Crypto: return Page::Crypto;
    default: return Page::Home;
  }
}

const char *tab_icon(uint8_t tab) {
  switch (static_cast<storage::Tab>(tab)) {
    case storage::Tab::Thermals: return PCD_SYM_THERMO;
    case storage::Tab::Player: return LV_SYMBOL_AUDIO;
    case storage::Tab::Today: return PCD_SYM_CLOUD_SUN;
    case storage::Tab::Crypto: return PCD_SYM_BITCOIN;
    default: return PCD_SYM_GAUGE;
  }
}

Str tab_label(uint8_t tab) {
  switch (static_cast<storage::Tab>(tab)) {
    case storage::Tab::Thermals: return Str::NavThermals;
    case storage::Tab::Player: return Str::NavControls;
    case storage::Tab::Today: return Str::NavToday;
    case storage::Tab::Crypto: return Str::NavCrypto;
    default: return Str::NavHome;
  }
}

void set_title(const char *text, uint32_t color) {
  set_text(s_title, text);
  set_color(s_title, color);
}

void set_subtitle(const char *text) {
  s_sub_custom = text != nullptr;
  if (text && !app::state().settings.demo_mode) set_text(s_sub, text);
  refresh_subtitle();
}

void apply_tab_order() {
  // Move the existing buttons (no rebuild): slot i shows tab_order[i]; Settings stays last.
  const uint8_t *order = app::state().settings.tab_order;
  for (int slot = 0; slot < storage::kTabCount; slot++) {
    const Page want = tab_page(order[slot]);
    for (auto &n : s_nav) {
      if (n.btn && n.page == want) lv_obj_move_to_index(n.btn, slot);
    }
  }
  // Hidden tabs leave the rail (the others share its height); their pages stay reachable in code.
  const uint8_t hidden = app::hidden_tabs();  // + the PC tabs while no PC is connected
  for (int t = 0; t < storage::kTabCount; t++) {
    const Page pg = tab_page(static_cast<uint8_t>(t));
    for (auto &n : s_nav) {
      if (!n.btn || n.page != pg) continue;
      if (hidden & (1u << t))
        lv_obj_add_flag(n.btn, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_clear_flag(n.btn, LV_OBJ_FLAG_HIDDEN);
    }
  }
  settings::on_tabs();
  app::crypto_sync();  // the Crypto tab may have been hidden / shown
}

void on_pc_presence() {
  apply_tab_order();  // the PC tabs leave / rejoin the rail
  if (!app::state().pc_present && is_pc_page(s_page)) {
    // Its data is gone: show the first tab that still works, and come back when the PC does.
    s_auto_from = parent_of(s_page);
    navigate(first_tab_page());
    s_auto_to = s_page;
    s_auto_moved = true;
  } else if (app::state().pc_present && s_auto_moved) {
    s_auto_moved = false;
    if (s_page == s_auto_to) navigate(s_auto_from);
  }
  screensaver::on_data();
}

void navigate(Page p) {
  for (int i = 0; i < kPageCount; i++) {
    if (!s_pages[i]) continue;
    if (i == static_cast<int>(p))
      lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
  }
  // Current page first: detail::show() renders the latest data immediately (it skips work for a
  // hidden page), so the view never flashes the other device's values for a second.
  s_page = p;
  s_sub_custom = false;
  default_title(p);
  if (parent_of(p) != p)
    lv_obj_clear_flag(s_back, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
  if (p == Page::Home) dashboard::update();  // fresh values the moment a metrics page opens
  if (p == Page::Thermals) thermals::update();
  if (p == Page::Today) today::update();
  if (p == Page::DetailCpu || p == Page::DetailGpu) {
    lv_obj_clear_flag(s_pages[static_cast<int>(Page::DetailCpu)], LV_OBJ_FLAG_HIDDEN);
    detail::show(p == Page::DetailGpu);
  }
  update_nav();
  refresh_subtitle();
  if (p == Page::Wifi) {
    wifi::update();
    wifi::on_show();
  }
  if (p == Page::Pair) pair::on_show();
  if (p == Page::Crypto) crypto_ui::on_show();
  app::crypto_sync();  // prices refresh faster while a Crypto page is visible
}

Page current_page() { return s_page; }

void dev_scroll(int y) {
  lv_obj_t *p = s_pages[static_cast<int>(s_page == Page::DetailGpu ? Page::DetailCpu : s_page)];
  if (p) lv_obj_scroll_to_y(p, y, LV_ANIM_OFF);
}

void build() {
  screensaver::on_rebuild();
  // Rebuild from scratch (language change): drop old tree, including the alert overlay.
  if (s_root) lv_obj_del(s_root);
  if (s_alert) lv_obj_del(s_alert);
  s_root = nullptr;
  s_alert = nullptr;

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, c(kBg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  s_root = lv_obj_create(scr);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

  build_nav(s_root);

  lv_obj_t *main = lv_obj_create(s_root);
  lv_obj_remove_style_all(main);
  lv_obj_set_height(main, LV_PCT(100));
  lv_obj_set_flex_grow(main, 1);
  lv_obj_set_flex_flow(main, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);

  build_top_bar(main);

  s_content = lv_obj_create(main);
  lv_obj_remove_style_all(s_content);
  lv_obj_set_width(s_content, LV_PCT(100));
  lv_obj_set_flex_grow(s_content, 1);
  lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_update_layout(s_root);  // pages size themselves from the content area

  s_pages[static_cast<int>(Page::Home)] = dashboard::create(s_content);
  s_pages[static_cast<int>(Page::Thermals)] = thermals::create(s_content);
  s_pages[static_cast<int>(Page::Controls)] = controls::create(s_content);
  s_pages[static_cast<int>(Page::Settings)] = settings::create(s_content);
  s_pages[static_cast<int>(Page::DetailCpu)] = detail::create(s_content);
  s_pages[static_cast<int>(Page::DetailGpu)] = nullptr;  // same page object, mode set by detail::show()
  s_pages[static_cast<int>(Page::Wifi)] = wifi::create(s_content);
  s_pages[static_cast<int>(Page::Pair)] = pair::create(s_content);
  s_pages[static_cast<int>(Page::Today)] = today::create(s_content);
  s_pages[static_cast<int>(Page::Crypto)] = crypto_ui::create(s_content);
  s_pages[static_cast<int>(Page::CryptoDetail)] = crypto_detail::create(s_content);

  apply_tab_order();  // hidden tabs (pages exist now, settings rows included)
  build_alert_overlay();
  if (s_first_build) {  // power-on: open the user's first tab
    s_first_build = false;
    s_page = first_tab_page();
    // The PC tabs join the rail when the PC app connects. If the user's first tab is one of them,
    // open it then, unless the user has picked another page meanwhile (on_pc_presence()).
    const auto &cfg = app::state().settings;
    for (int i = 0; i < storage::kTabCount; i++) {
      if (cfg.tab_hidden & (1u << cfg.tab_order[i])) continue;
      const Page first = tab_page(cfg.tab_order[i]);
      if (first != s_page) {
        s_auto_from = first;
        s_auto_to = s_page;
        s_auto_moved = true;
      }
      break;
    }
  }
  // A sub-page cannot be restored on its own after a rebuild: open its parent.
  Page start = parent_of(s_page);
  if (!app::state().pc_present && is_pc_page(start)) start = first_tab_page();
  navigate(start);
  on_link();
  on_telemetry();
  setup::on_rebuild();
}

void on_telemetry() {
  const auto &st = app::state();
  refresh_subtitle();
  // Metrics repaint at the user's pace (Settings > Display); history keeps 1 s resolution and the
  // player keeps following the PC every second.
  const uint32_t now = lv_tick_get();
  const uint32_t period = st.settings.refresh_s * 1000u;
  const bool warming = static_cast<int32_t>(s_warm_until - now) > 0;
  if (s_metrics_force || warming || now - s_metrics_ms + 150 >= period) {
    s_metrics_force = false;
    s_metrics_ms = now;
    dashboard::update();
    thermals::update();
    detail::update();
  }
  controls::on_telemetry();
  screensaver::on_data();
}

void on_link() {
  const auto &st = app::state();
  const bool demo = st.settings.demo_mode;
  const bool live = st.link == app::LinkState::Online;
  if (live && !s_was_live) {  // boot or reconnect: show data at once, then settle to the period
    s_metrics_force = true;
    s_warm_until = lv_tick_get() + kWarmMs;
  }
  s_was_live = live;
  // Status LED: green = PC app connected (live or briefly stale), grey = not; amber + DEMO in demo mode.
  const bool pc = st.link == app::LinkState::Online || st.link == app::LinkState::Stale;
  const bool lit = demo || pc;
  const uint32_t led = demo ? kPower : pc ? kOk : kLedOff;
  lv_obj_set_style_bg_color(s_pill_dot, c(led), 0);
  lv_obj_set_style_border_color(s_pill_dot, c(lit ? led : kLedOffRim), 0);
  lv_obj_set_style_shadow_color(s_pill_dot, c(led), 0);
  lv_obj_set_style_shadow_opa(s_pill_dot, lit ? LV_OPA_80 : LV_OPA_TRANSP, 0);
  set_text(s_pill_lbl, demo ? tr(Str::StateDemo) : "");
  if (demo)
    lv_obj_clear_flag(s_pill_lbl, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(s_pill_lbl, LV_OBJ_FLAG_HIDDEN);
  refresh_subtitle();
  controls::update();
  settings::update();
  today::update();
  crypto_ui::on_data();  // connection state changes the empty-state message
  screensaver::on_data();  // PC and music cards only while the PC is there
}

void on_command_result(const app::CommandResult &r) { controls::on_result(r); }

void on_alert(uint8_t mask) {
  const auto &st = app::state();
  const bool cpu = mask & 0x01;
  const auto level = cpu ? st.cpu_alert.level() : st.gpu_alert.level();
  const float temp = cpu ? st.snap.cpu.temp_c : st.snap.gpu.temp_c;
  const bool crit = level == alerts::Level::Critical;
  char t[24];
  fmt_temp(t, sizeof(t), temp);
  char buf[128];
  snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s · %s %s\n%s", crit ? tr(Str::AlertCritical) : tr(Str::AlertWarning),
           cpu ? "CPU" : "GPU", t, tr(Str::AlertTapToDismiss));
  lv_label_set_text(s_alert_lbl, buf);
  lv_obj_set_style_bg_color(s_alert, c(crit ? kCritical : kWarning), 0);
  lv_obj_clear_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_alert);
}

void on_art(const app::ArtFrame &art) {
  controls::on_art(art);
  screensaver::on_data();
}

// ---- toast ------------------------------------------------------------------------------------
namespace {
lv_obj_t *s_toast = nullptr;
lv_obj_t *s_toast_lbl = nullptr;
lv_timer_t *s_toast_timer = nullptr;
}  // namespace

void toast(const char *text, uint32_t color) {
  if (!s_toast) {
    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast, kContentW - 2 * kMargin, 0);
    lv_obj_set_style_bg_color(s_toast, theme::c(theme::kSurfaceHi), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, 16, 0);
    lv_obj_set_style_pad_hor(s_toast, 14, 0);
    lv_obj_set_style_pad_ver(s_toast, 8, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    s_toast_lbl = theme::make_label(s_toast, theme::font_small(), theme::kText, "");
    lv_label_set_long_mode(s_toast_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(s_toast_lbl, kContentW - 2 * kMargin - 28, 0);
  }
  lv_label_set_text(s_toast_lbl, text);
  lv_obj_set_style_border_color(s_toast, theme::c(color), 0);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, kNavW / 2, -10);
  lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_toast);
  if (s_toast_timer) lv_timer_del(s_toast_timer);
  s_toast_timer = lv_timer_create(
      [](lv_timer_t *) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        s_toast_timer = nullptr;
      },
      2600, nullptr);
  lv_timer_set_repeat_count(s_toast_timer, 1);
}

// ---- confirmation dialog -------------------------------------------------------------------------------
namespace {
lv_obj_t *s_confirm = nullptr;
void (*s_confirm_cb)(void *) = nullptr;
void *s_confirm_user = nullptr;

void close_confirm() {
  if (s_confirm) {
    lv_obj_del_async(s_confirm);
    s_confirm = nullptr;
  }
}
}  // namespace

void confirm(const char *text, const char *ok_label, uint32_t ok_color, void (*on_ok)(void *), void *user) {
  using namespace theme;
  close_confirm();
  s_confirm_cb = on_ok;
  s_confirm_user = user;
  s_confirm = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_confirm);
  lv_obj_set_size(s_confirm, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_confirm, c(0x000000), 0);
  lv_obj_set_style_bg_opa(s_confirm, LV_OPA_70, 0);
  lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_CLICKABLE);  // swallow touches behind the dialog

  lv_obj_t *box = make_card(s_confirm);
  lv_obj_set_size(box, 340, LV_SIZE_CONTENT);
  lv_obj_center(box);
  lv_obj_set_style_pad_all(box, 14, 0);
  lv_obj_set_style_pad_row(box, 12, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_t *msg = make_label(box, font_body(), kText, text);
  lv_obj_set_width(msg, LV_PCT(100));
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

  lv_obj_t *row = lv_obj_create(box);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_t *cancel = make_button(row, tr(Str::Cancel));
  lv_obj_set_flex_grow(cancel, 1);
  lv_obj_add_event_cb(cancel, [](lv_event_t *) { close_confirm(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *ok = make_button(row, ok_label);
  lv_obj_set_flex_grow(ok, 1);
  lv_obj_set_style_bg_color(ok, lv_color_mix(c(ok_color), c(kBg), 70), 0);
  lv_obj_set_style_border_color(ok, c(ok_color), 0);
  lv_obj_add_event_cb(
      ok,
      [](lv_event_t *) {
        close_confirm();
        if (s_confirm_cb) s_confirm_cb(s_confirm_user);
      },
      LV_EVENT_CLICKED, nullptr);
}

void on_weather() {
  today::update();
  settings::on_place();
  screensaver::on_data();
}
void on_places() { place::on_results(); }
void on_wifi_scan() { wifi::on_scan(); }
void on_discovery() { pair::on_discovery(); }
void on_pair_done(const app::PairOutcome &r) { pair::on_pair_done(r); }
void on_crypto() {
  crypto_ui::on_data();
  crypto_detail::on_data();
  screensaver::on_data();
}
void on_crypto_chart() { crypto_detail::on_chart(); }
void on_crypto_watch() {
  crypto_ui::on_watch();
  settings::on_crypto();
}

void tick_1s() {
  // Re-evaluate staleness visuals and diagnostics once per second.
  update_deck_battery();
  settings::tick();
  today::tick();
  crypto_ui::tick();
  crypto_detail::tick();
  setup::tick();
  screensaver::tick();
  if (s_page == Page::Wifi) wifi::update();
  wifi::tick();
  if (s_page == Page::Pair) pair::update();
  // Auto-hide the alert once both sensors are back to normal.
  const auto &st = app::state();
  if (s_alert && !lv_obj_has_flag(s_alert, LV_OBJ_FLAG_HIDDEN) && st.cpu_alert.level() == alerts::Level::Normal &&
      st.gpu_alert.level() == alerts::Level::Normal) {
    lv_obj_add_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace ui
