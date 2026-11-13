// Crypto tab (ported from the JC4827W543 deck / JC1060P470C CryptoTracker, portrait 296x354):
//   header:        currency · price age ........................................ [coins]
//   global strip:  total market cap (+24 h %), BTC dominance, Fear & Greed (value, class, gauge)
//   coin list:     cards in the JC1060P470C style: brand-colour accent · logo · symbol #rank / name ·
//                  24 h sparkline · price / 24 h change pill (tap = detail)
// Prices come from the PC app (Binance / CoinGecko / alternative.me), already converted to the
// chosen currency.
//
// Also the coin picker sheet (Settings > Crypto > Coins, the header button or the empty state).

#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "crypto/coin_catalog.h"
#include "crypto/crypto_store.h"
#include "crypto/watchlist.h"
#include "esp_heap_caps.h"
#include "network/net_task.h"
#include "ui/chart_render.h"
#include "ui/crypto_fmt.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace {

// Fear & Greed: colour and translated class (alternative.me thresholds).
uint32_t fng_color(int v) {
  if (v < 0) return theme::kMuted;
  if (v < 25) return 0xF05A5A;
  if (v < 45) return 0xF59E0B;
  if (v <= 55) return 0xE5D36B;
  if (v <= 75) return 0x9BD86A;
  return 0x34D399;
}

Str fng_class(int v) {
  if (v < 25) return Str::FngExtremeFear;
  if (v < 45) return Str::FngFear;
  if (v <= 55) return Str::FngNeutral;
  if (v <= 75) return Str::FngGreed;
  return Str::FngExtremeGreed;
}

}  // namespace

namespace crypto_ui {
namespace {

using namespace theme;

// Coin rows: two tiers with the same elements and type scale as the JC1060P470C card:
//   top:     accent · logo · symbol #rank / name ........ price (20 px) / change in currency + "24h"
//   bottom:  wide 24 h sparkline ........................................... 24 h change pill
// Content box: 456 - 2 (border) - 16 (left, holds the accent) - 10 (right) = 428 px. Widths measured
// with the fonts: a BTC price at 20 px = 108..111, "-4.090,28 $  24h" at 14 px = 112, "▼ -10,25%" = 63.
constexpr int kRowH = 80;
constexpr int kCardW = kScreenW - 2 * kMargin - 28;  // 428
constexpr int kTextX = 42;                     // symbol / name, right of the 32 px logo
constexpr int kRightW = 150;                   // price / change column
constexpr int kTextW = kCardW - kTextX - kRightW - 6;  // 230
constexpr int kTier2Y = 47;                    // sparkline and pill
constexpr int kSparkW = 318;                   // the width the 24 h pill leaves
constexpr int kSparkH = 28;
constexpr int kPillW = kCardW - kSparkW - 10;  // 100
constexpr int kPillH = 24;

struct Row {
  lv_obj_t *root = nullptr;
  lv_obj_t *logo = nullptr;
  lv_obj_t *sym = nullptr;
  lv_obj_t *rank = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *spark = nullptr;
  lv_obj_t *price = nullptr;
  lv_obj_t *delta = nullptr;  // 24 h change in currency, under the price
  lv_obj_t *pill = nullptr;
  lv_obj_t *chg = nullptr;
  int coin = -1;
};

struct Block {
  lv_obj_t *cap = nullptr;
  lv_obj_t *value = nullptr;
  lv_obj_t *delta = nullptr;
};

lv_obj_t *s_page = nullptr;
lv_obj_t *s_age = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_empty = nullptr;
lv_obj_t *s_empty_icon = nullptr;
lv_obj_t *s_empty_lbl = nullptr;
lv_obj_t *s_empty_btn = nullptr;
lv_obj_t *s_spinner = nullptr;
lv_obj_t *s_fng_bar = nullptr;
lv_obj_t *s_fng_knob = nullptr;
Block s_mc, s_dom, s_fng;
Row s_rows[crypto::kMaxCoins];
int s_row_count = 0;
uint16_t *s_spark_buf[crypto::kMaxCoins] = {};
crypto::Market *s_m = nullptr;  // UI copy (PSRAM)
bool s_have = false;
uint32_t s_rx_ms = 0;

void row_cb(lv_event_t *e) {
  const int coin = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  crypto_detail::show(coin);
}

// Caption on top, value below; the delta (or F&G class) under the value. w = 0: takes the rest.
Block make_block(lv_obj_t *parent, const char *caption, int w) {
  Block b;
  lv_obj_t *k = make_card(parent);
  lv_obj_set_height(k, LV_PCT(100));
  if (w > 0)
    lv_obj_set_width(k, w);
  else
    lv_obj_set_flex_grow(k, 1);
  lv_obj_set_style_pad_ver(k, 4, 0);
  lv_obj_set_style_pad_hor(k, 8, 0);
  b.cap = make_label(k, font_tiny(), kMuted, caption);
  lv_obj_set_size(b.cap, LV_PCT(100), lv_font_get_line_height(font_tiny()));
  lv_label_set_long_mode(b.cap, LV_LABEL_LONG_DOT);
  lv_obj_align(b.cap, LV_ALIGN_TOP_LEFT, 0, -1);
  b.value = make_label(k, font_body(), kText, "--");
  lv_obj_align(b.value, LV_ALIGN_TOP_LEFT, 0, 14);
  b.delta = make_label(k, font_tiny(), kMuted, "");
  lv_obj_set_width(b.delta, LV_PCT(100));
  lv_label_set_long_mode(b.delta, LV_LABEL_LONG_DOT);
  lv_obj_align(b.delta, LV_ALIGN_TOP_LEFT, 0, 33);
  return b;
}

const crypto::Coin *find_coin(const char *pair) {
  if (!s_have) return nullptr;
  for (int i = 0; i < s_m->count; i++)
    if (!strcmp(s_m->coins[i].pair, pair)) return &s_m->coins[i];
  return nullptr;
}

void render_spark(int r, const crypto::Coin *cn) {
  uint16_t *buf = s_spark_buf[r];
  if (!buf) return;
  chart::Paint p = {};
  p.bg = kSurface;
  p.line_w = 2.0f;
  p.glow_r = 3.0f;
  p.glow_a = 40;
  p.fill_a = 80;
  p.pad_top = 3;
  p.pad_bottom = 2;
  if (!cn || cn->spark_n < 2) {
    chart::clear(buf, kSparkW, kSparkH, kSurface);
  } else {
    float v[crypto::kSparkPoints];
    for (int i = 0; i < cn->spark_n; i++) v[i] = cn->spark[i];
    const bool up = cn->spark[cn->spark_n - 1] >= cn->spark[0];
    p.line = up ? kUp : kDown;
    chart::render_line(buf, kSparkW, kSparkH, v, cn->spark_n, -4.0f, 104.0f, p);
  }
  lv_obj_invalidate(s_rows[r].spark);
}

void fill_row(int r) {
  Row &row = s_rows[r];
  const crypto::Coin *cn = find_coin(COIN_CATALOG[row.coin].pair);
  const char *cur = s_have ? s_m->cur : "USD";
  char b[40];
  if (cn && crypto::has(cn->price)) {
    cfmt::money(cn->price, -1, cur, b, sizeof(b));
    set_text(row.price, b);
    set_color(row.price, kText);
  } else {
    set_text(row.price, "--");
    set_color(row.price, kMuted);
  }
  if (cn && !isnan(cn->chg_pct)) {
    char p[24];
    cfmt::pct(cn->chg_pct, p, sizeof(p));
    const bool up = cn->chg_pct >= 0;
    snprintf(b, sizeof(b), "%s %s", up ? PCD_SYM_CARET_UP : PCD_SYM_CARET_DOWN, p);
    set_text(row.chg, b);
    lv_obj_set_style_bg_color(row.pill, c(up ? 0x123B2C : 0x45191C), 0);
    lv_obj_set_style_border_color(row.pill, c(up ? kUp : kDown), 0);
    set_color(row.chg, up ? kUp : kDown);
  } else {
    set_text(row.chg, "--");
    lv_obj_set_style_bg_color(row.pill, c(kSurfaceHi), 0);
    lv_obj_set_style_border_color(row.pill, c(kBorder), 0);
    set_color(row.chg, kMuted);
  }
  // 24 h change in currency under the price, as on the JC1060P470C.
  if (cn && crypto::has(cn->chg_abs) && !isnan(cn->chg_pct)) {
    char m[32];
    cfmt::money(cn->chg_abs >= 0 ? cn->chg_abs : -cn->chg_abs, -1, cur, m, sizeof(m));
    snprintf(b, sizeof(b), "%s%s  24h", cn->chg_abs >= 0 ? "+" : "-", m);
    set_text(row.delta, b);
    set_color(row.delta, cn->chg_abs >= 0 ? kUp : kDown);
  } else {
    set_text(row.delta, "");
  }
  // Market-cap rank right of the symbol, only when it stays clear of the price column.
  b[0] = '\0';
  if (cn && cn->rank > 0) snprintf(b, sizeof(b), "#%d", cn->rank);
  const char *sym = COIN_CATALOG[row.coin].sym;
  const lv_coord_t sym_w = lv_txt_get_width(sym, strlen(sym), font_body(), 0, LV_TEXT_FLAG_NONE);
  const lv_coord_t rank_w = lv_txt_get_width(b, strlen(b), font_tiny(), 0, LV_TEXT_FLAG_NONE);
  if (b[0] && kTextX + sym_w + 6 + rank_w <= kCardW - kRightW - 6) {
    set_text(row.rank, b);
    lv_obj_set_x(row.rank, kTextX + sym_w + 6);
  } else {
    set_text(row.rank, "");
  }
  render_spark(r, cn);
}

void build_rows() {
  lv_obj_clean(s_list);
  s_row_count = 0;
  int8_t idx[crypto::kMaxCoins];
  const int n = crypto::parse_watch(app::state().settings.crypto_coins, idx, crypto::kMaxCoins);
  for (int i = 0; i < n; i++) {
    Row &r = s_rows[s_row_count];
    r = Row{};
    r.coin = idx[i];
    const coin_info_t &ci = COIN_CATALOG[r.coin];
    r.root = lv_obj_create(s_list);
    lv_obj_remove_style_all(r.root);
    lv_obj_set_size(r.root, LV_PCT(100), kRowH);
    lv_obj_set_style_radius(r.root, 14, 0);
    lv_obj_set_style_bg_color(r.root, c(kSurface), 0);
    lv_obj_set_style_bg_opa(r.root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r.root, c(kSurfaceHi), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(r.root, c(kBorder), 0);
    lv_obj_set_style_border_opa(r.root, LV_OPA_50, 0);
    lv_obj_set_style_border_width(r.root, 1, 0);
    lv_obj_set_style_pad_left(r.root, 16, 0);
    lv_obj_set_style_pad_right(r.root, 10, 0);
    lv_obj_add_flag(r.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(r.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(r.root, row_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(r.coin)));
    // Brand-colour accent on the left edge, halfway between the border and the logo.
    lv_obj_t *accent = lv_obj_create(r.root);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 3, kRowH - 30);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_bg_color(accent, c(ci.color), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, -9, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE);
    // Top tier. Baselines: symbol (16 px) on the price's (20 px), name and change (14 px) on one line.
    r.logo = lv_img_create(r.root);
    lv_img_set_src(r.logo, ci.logo_m);
    lv_obj_align(r.logo, LV_ALIGN_TOP_LEFT, 0, 8);
    r.sym = make_label(r.root, font_body(), kText, ci.sym);
    lv_obj_align(r.sym, LV_ALIGN_TOP_LEFT, kTextX, 3);
    r.rank = make_label(r.root, font_tiny(), kMuted, "");
    lv_obj_set_y(r.rank, 8);
    r.name = make_label(r.root, font_small(), kMuted, ci.name);
    lv_obj_set_size(r.name, kTextW, lv_font_get_line_height(font_small()));
    lv_label_set_long_mode(r.name, LV_LABEL_LONG_DOT);
    lv_obj_align(r.name, LV_ALIGN_TOP_LEFT, kTextX, 25);
    r.price = make_label(r.root, font_title(), kText, "--");
    lv_obj_set_size(r.price, kRightW, lv_font_get_line_height(font_title()));
    lv_label_set_long_mode(r.price, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(r.price, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(r.price, LV_ALIGN_TOP_RIGHT, 0, -1);
    r.delta = make_label(r.root, font_small(), kMuted, "");
    lv_obj_set_size(r.delta, kRightW, lv_font_get_line_height(font_small()));
    lv_label_set_long_mode(r.delta, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(r.delta, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(r.delta, LV_ALIGN_TOP_RIGHT, 0, 25);
    // Bottom tier: the sparkline across the card, the pill at its end.
    r.spark = lv_canvas_create(r.root);
    if (s_spark_buf[s_row_count]) {
      lv_canvas_set_buffer(r.spark, s_spark_buf[s_row_count], kSparkW, kSparkH, LV_IMG_CF_TRUE_COLOR);
      chart::clear(s_spark_buf[s_row_count], kSparkW, kSparkH, kSurface);
    }
    lv_obj_align(r.spark, LV_ALIGN_TOP_LEFT, 0, kTier2Y);
    lv_obj_clear_flag(r.spark, LV_OBJ_FLAG_CLICKABLE);
    r.pill = lv_obj_create(r.root);
    lv_obj_remove_style_all(r.pill);
    lv_obj_set_size(r.pill, kPillW, kPillH);
    lv_obj_set_style_radius(r.pill, 8, 0);
    lv_obj_set_style_bg_opa(r.pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r.pill, c(kSurfaceHi), 0);
    lv_obj_set_style_border_width(r.pill, 1, 0);
    lv_obj_set_style_border_opa(r.pill, LV_OPA_40, 0);
    lv_obj_align(r.pill, LV_ALIGN_TOP_RIGHT, 0, kTier2Y + (kSparkH - kPillH) / 2);
    lv_obj_clear_flag(r.pill, LV_OBJ_FLAG_CLICKABLE);
    r.chg = make_label(r.pill, font_small(), kMuted, "--");  // 14 px: carries the caret icons
    lv_obj_center(r.chg);
    s_row_count++;
  }
  for (int i = 0; i < s_row_count; i++) fill_row(i);
}

void fill_global() {
  char b[40];
  const char *cur = s_have ? s_m->cur : "USD";
  if (s_have && crypto::has(s_m->mcap)) {
    cfmt::compact_money(s_m->mcap, cur, b, sizeof(b));
    set_text(s_mc.value, b);
  } else {
    set_text(s_mc.value, "--");
  }
  if (s_have && !isnan(s_m->mcap_chg)) {
    cfmt::pct(s_m->mcap_chg, b, sizeof(b));
    set_text(s_mc.delta, b);
    set_color(s_mc.delta, s_m->mcap_chg >= 0 ? kUp : kDown);
  } else {
    set_text(s_mc.delta, "");
  }
  if (s_have && !isnan(s_m->btc_dom)) {
    char n[16];
    cfmt::number(s_m->btc_dom, 1, n, sizeof(n));
    snprintf(b, sizeof(b), "%s %%", n);
    set_text(s_dom.value, b);
  } else {
    set_text(s_dom.value, "--");
  }
  const int f = s_have ? s_m->fng : -1;
  if (f >= 0) {
    snprintf(b, sizeof(b), "%d", f);
    set_text(s_fng.value, b);
    set_text(s_fng.delta, tr(fng_class(f)));
    set_color(s_fng.delta, fng_color(f));
    set_color(s_fng.value, fng_color(f));
    lv_obj_clear_flag(s_fng_knob, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_fng_bar);
    const lv_coord_t w = lv_obj_get_width(s_fng_bar);
    lv_obj_set_x(s_fng_knob, static_cast<lv_coord_t>(f * (w - 4) / 100));
  } else {
    set_text(s_fng.value, "--");
    set_color(s_fng.value, kText);
    set_text(s_fng.delta, "");
    lv_obj_add_flag(s_fng_knob, LV_OBJ_FLAG_HIDDEN);
  }
}

void show_empty(const char *text, const char *icon, bool spinner, bool button) {
  set_text(s_empty_lbl, text);
  set_text(s_empty_icon, icon ? icon : "");
  if (icon) lv_obj_clear_flag(s_empty_icon, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(s_empty_icon, LV_OBJ_FLAG_HIDDEN);
  if (spinner) lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
  if (button) lv_obj_clear_flag(s_empty_btn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(s_empty_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
}

void show_list() {
  lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
}

// List, or why there is nothing to show.
void refresh_state() {
  if (!s_page) return;
  if (s_row_count == 0) {
    show_empty(tr(Str::CryNoCoins), PCD_SYM_COINS, false, true);
    return;
  }
  if (s_have) {
    show_list();
    return;
  }
  char err[24];
  crypto::market_error(err, sizeof(err));
  const auto &st = app::state();
  if (!strcmp(err, "pc_app_old"))
    show_empty(tr(Str::CryOldApp), LV_SYMBOL_WARNING, false, false);
  else if (!app::data_online())
    show_empty(tr(Str::CryOffline), LV_SYMBOL_WIFI, false, false);
  else if (err[0])
    show_empty(tr(Str::CryFailed), LV_SYMBOL_WARNING, false, false);
  else
    show_empty(tr(Str::CryLoading), nullptr, true, false);
}

// Header line: "EUR  ·  act. hace 12 s".
void refresh_age() {
  if (!s_age || current_page() != Page::Crypto) return;  // each repaint is a full-frame push
  const char *cur = s_have ? s_m->cur : cfmt::currency(app::state().settings.crypto_cur).code;
  char b[72];
  if (s_have) {
    const uint32_t age = (lv_tick_get() - s_rx_ms) / 1000;
    char a[16], ago[40];
    if (age < 120)
      snprintf(a, sizeof(a), "%u s", static_cast<unsigned>(age));
    else
      snprintf(a, sizeof(a), "%u min", static_cast<unsigned>(age / 60));
    if (age < 5)
      snprintf(ago, sizeof(ago), "%s", tr(Str::CryJustNow));
    else
      snprintf(ago, sizeof(ago), tr(Str::CryUpdatedFmt), a);
    snprintf(b, sizeof(b), "%s  ·  %s", cur, ago);
  } else {
    snprintf(b, sizeof(b), "%s", cur);
  }
  set_text(s_age, b);
}

lv_obj_t *pill_button(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_bg_color(b, c(kCrypto), 0);
  lv_obj_set_style_bg_color(b, c(0xD08A24), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_ext_click_area(b, 6);
  lv_obj_t *l = make_label(b, font_small(), kBg, text);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  if (!s_m) {
    void *mem = heap_caps_malloc(sizeof(crypto::Market), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem) {
      s_m = new (mem) crypto::Market;
      crypto::reset(*s_m);
    }
    for (auto &b : s_spark_buf)
      b = static_cast<uint16_t *>(heap_caps_malloc(kSparkW * kSparkH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  s_page = make_page(parent, false);
  lv_obj_set_style_pad_row(s_page, kGap, 0);
  lv_obj_set_style_pad_top(s_page, 6, 0);

  // Header: currency and price age, coin picker on the right.
  lv_obj_t *head = lv_obj_create(s_page);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), 30);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *ico = make_label(head, font_body(), kCrypto, PCD_SYM_BITCOIN);
  lv_obj_align(ico, LV_ALIGN_LEFT_MID, 2, 0);
  s_age = make_label(head, font_small(), kMuted, "");
  lv_obj_set_width(s_age, 330);
  lv_label_set_long_mode(s_age, LV_LABEL_LONG_DOT);
  lv_obj_align(s_age, LV_ALIGN_LEFT_MID, 26, 0);
  lv_obj_t *cb = pill_button(head, PCD_SYM_COINS " " LV_SYMBOL_PLUS, 64, 28, [](lv_event_t *) { coins::open(); });
  lv_obj_align(cb, LV_ALIGN_RIGHT_MID, 0, 0);

  // Global strip.
  lv_obj_t *strip = lv_obj_create(s_page);
  lv_obj_remove_style_all(strip);
  lv_obj_set_size(strip, LV_PCT(100), 58);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(strip, 4, 0);
  lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
  // Widths follow the 12 px captions (Spanish is the longest): "Capitalización" ~88 px, "Dom. BTC"
  // ~62 px, "Miedo y codicia" / "Codicia extrema" ~100 px in the block that takes the rest (114).
  s_mc = make_block(strip, tr(Str::CryMarketCap), 150);
  s_dom = make_block(strip, tr(Str::CryBtcDom), 120);
  s_fng = make_block(strip, tr(Str::CryFng), 0);
  // Fear & Greed gauge: red -> green track with a knob at the value, under the number.
  lv_obj_t *fk = lv_obj_get_parent(s_fng.value);
  lv_obj_set_width(s_fng.delta, LV_PCT(100));
  s_fng_bar = lv_obj_create(fk);
  lv_obj_remove_style_all(s_fng_bar);
  lv_obj_set_size(s_fng_bar, 34, 3);
  lv_obj_set_style_radius(s_fng_bar, 2, 0);
  lv_obj_set_style_bg_opa(s_fng_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_fng_bar, c(0xF05A5A), 0);
  lv_obj_set_style_bg_grad_color(s_fng_bar, c(0x34D399), 0);
  lv_obj_set_style_bg_grad_dir(s_fng_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_align(s_fng_bar, LV_ALIGN_TOP_RIGHT, 0, 22);
  lv_obj_clear_flag(s_fng_bar, LV_OBJ_FLAG_SCROLLABLE);
  s_fng_knob = lv_obj_create(s_fng_bar);
  lv_obj_remove_style_all(s_fng_knob);
  lv_obj_set_size(s_fng_knob, 4, 9);
  lv_obj_set_style_radius(s_fng_knob, 2, 0);
  lv_obj_set_style_bg_color(s_fng_knob, c(kText), 0);
  lv_obj_set_style_bg_opa(s_fng_knob, LV_OPA_COVER, 0);
  lv_obj_set_y(s_fng_knob, -3);
  lv_obj_add_flag(s_fng_knob, LV_OBJ_FLAG_HIDDEN);

  // Coin list (scrolls).
  s_list = lv_obj_create(s_page);
  lv_obj_remove_style_all(s_list);
  lv_obj_set_width(s_list, LV_PCT(100));
  lv_obj_set_flex_grow(s_list, 1);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_list, 6, 0);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);

  // Empty / status state.
  s_empty = make_card(s_page);
  lv_obj_set_width(s_empty, LV_PCT(100));
  lv_obj_set_flex_grow(s_empty, 1);
  lv_obj_set_flex_flow(s_empty, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_empty, 14, 0);
  s_empty_icon = make_label(s_empty, font_value(), kCrypto, PCD_SYM_COINS);
  s_spinner = lv_spinner_create(s_empty, 1000, 60);
  lv_obj_set_size(s_spinner, 34, 34);
  lv_obj_set_style_arc_width(s_spinner, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_spinner, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_spinner, c(kCrypto), LV_PART_INDICATOR);
  s_empty_lbl = make_label(s_empty, font_body(), kText, "");
  lv_obj_set_width(s_empty_lbl, LV_PCT(92));
  lv_label_set_long_mode(s_empty_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
  s_empty_btn = pill_button(s_empty, tr(Str::CryChooseCoins), 200, kTouchMin, [](lv_event_t *) { coins::open(); });
  lv_obj_set_style_text_font(lv_obj_get_child(s_empty_btn, 0), font_body(), 0);

  s_have = crypto::get_market(*s_m, &s_rx_ms);
  build_rows();
  fill_global();
  refresh_state();
  return s_page;
}

void on_show() {
  refresh_age();
  refresh_state();
  // Coming back after a while: ask for fresh prices instead of waiting for the next period.
  const uint32_t refresh_ms = app::state().settings.crypto_refresh_s * 1000u;
  if (!s_have || lv_tick_get() - s_rx_ms > refresh_ms) net::request_crypto();
}

void on_data() {
  if (!s_page || !s_m) return;
  uint32_t rx = 0;
  const bool have = crypto::get_market(*s_m, &rx);
  if (have && (!s_have || rx != s_rx_ms)) {
    s_have = true;
    s_rx_ms = rx;
    for (int i = 0; i < s_row_count; i++) fill_row(i);
    fill_global();
  }
  refresh_state();
  refresh_age();
}

void on_watch() {
  if (!s_page) return;
  build_rows();
  fill_global();
  refresh_state();
  refresh_age();
}

void tick() { refresh_age(); }

}  // namespace crypto_ui

// ---- coin picker sheet ---------------------------------------------------------------------------
namespace coins {
namespace {

using namespace theme;

constexpr int kMaxChips = 64;
lv_obj_t *s_sheet = nullptr;
lv_obj_t *s_count = nullptr;
lv_obj_t *s_chip[kMaxChips] = {};
lv_obj_t *s_badge[kMaxChips] = {};
int8_t s_sel[crypto::kMaxCoins];
int s_sel_n = 0;

void close() {
  if (s_sheet) {
    lv_obj_del_async(s_sheet);
    s_sheet = nullptr;
  }
}

int sel_pos(int coin) {
  for (int i = 0; i < s_sel_n; i++)
    if (s_sel[i] == coin) return i;
  return -1;
}

void render() {
  char b[32];
  snprintf(b, sizeof(b), tr(Str::CryCoinsCountFmt), s_sel_n, crypto::kMaxCoins);
  set_text(s_count, b);
  for (int i = 0; i < COIN_CATALOG_COUNT && i < kMaxChips; i++) {
    const int pos = sel_pos(i);
    const bool on = pos >= 0;
    lv_obj_set_style_border_color(s_chip[i], c(on ? kCrypto : kBorder), 0);
    lv_obj_set_style_border_width(s_chip[i], on ? 2 : 1, 0);
    lv_obj_set_style_bg_color(s_chip[i], c(on ? kSurfaceHi : kSurface), 0);
    if (on) {
      snprintf(b, sizeof(b), "%d", pos + 1);
      set_text(s_badge[i], b);
      lv_obj_clear_flag(s_badge[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_badge[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void chip_cb(lv_event_t *e) {
  const int coin = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const int pos = sel_pos(coin);
  if (pos >= 0) {
    for (int i = pos; i + 1 < s_sel_n; i++) s_sel[i] = s_sel[i + 1];
    s_sel_n--;
  } else if (s_sel_n < crypto::kMaxCoins) {
    s_sel[s_sel_n++] = static_cast<int8_t>(coin);
  } else {
    char b[48];
    snprintf(b, sizeof(b), tr(Str::CryCoinsMaxFmt), crypto::kMaxCoins);
    toast(b, kWarning);
    return;
  }
  render();
}

void done_cb(lv_event_t *) {
  char csv[sizeof(storage::Settings::crypto_coins)];
  crypto::format_watch(s_sel, s_sel_n, csv, sizeof(csv));
  close();
  app::set_crypto_coins(csv);
}

}  // namespace

void open() {
  close();
  s_sel_n = crypto::parse_watch(app::state().settings.crypto_coins, s_sel, crypto::kMaxCoins);
  s_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_sheet);
  lv_obj_set_size(s_sheet, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_sheet, c(kBg), 0);
  lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_sheet, kMargin, 0);
  lv_obj_set_style_pad_row(s_sheet, 8, 0);
  lv_obj_set_flex_flow(s_sheet, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE);

  // Header: [x] Coins ........ 3 of 12 [Done]
  lv_obj_t *head = lv_obj_create(s_sheet);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), 40);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 8, 0);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *x = lv_btn_create(head);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, 36, 36);
  lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(x, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(x, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(x, c(kBorder), LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(x, 8);
  lv_obj_t *xl = make_label(x, font_body(), kText, LV_SYMBOL_CLOSE);
  lv_obj_center(xl);
  lv_obj_add_event_cb(x, [](lv_event_t *) { close(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *title = make_label(head, font_title(), kText, tr(Str::CryCoinsTitle));
  lv_obj_set_flex_grow(title, 1);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  s_count = make_label(head, font_small(), kCrypto, "");
  lv_obj_t *ok = lv_btn_create(head);
  lv_obj_remove_style_all(ok);
  lv_obj_set_size(ok, 80, 36);
  lv_obj_set_style_radius(ok, 18, 0);
  lv_obj_set_style_bg_color(ok, c(kCrypto), 0);
  lv_obj_set_style_bg_color(ok, c(0xD08A24), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, 0);
  lv_obj_set_ext_click_area(ok, 6);
  lv_obj_t *okl = make_label(ok, font_body(), kBg, tr(Str::CryDone));
  lv_obj_center(okl);
  lv_obj_add_event_cb(ok, done_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *hint = make_label(s_sheet, font_small(), kMuted, tr(Str::CryCoinsHint));
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

  // Grid of chips, 3 per row, scrolls.
  lv_obj_t *grid = lv_obj_create(s_sheet);
  lv_obj_remove_style_all(grid);
  lv_obj_set_width(grid, LV_PCT(100));
  lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 6, 0);
  lv_obj_set_style_pad_column(grid, 6, 0);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);
  const int chip_w = (kScreenW - 2 * kMargin - 3 * 6) / 4;  // 109: four per row
  for (int i = 0; i < COIN_CATALOG_COUNT && i < kMaxChips; i++) {
    const coin_info_t &ci = COIN_CATALOG[i];
    lv_obj_t *chip = lv_obj_create(grid);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, chip_w, 44);
    lv_obj_set_style_radius(chip, 10, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(chip, 6, 0);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(chip, chip_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_t *logo = lv_img_create(chip);
    lv_img_set_src(logo, ci.logo_s);
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *sym = make_label(chip, font_small(), kText, ci.sym);
    lv_obj_set_size(sym, chip_w - 12 - 28, lv_font_get_line_height(font_small()));  // one line
    lv_label_set_long_mode(sym, LV_LABEL_LONG_DOT);
    lv_obj_align(sym, LV_ALIGN_LEFT_MID, 28, 0);
    s_badge[i] = make_label(chip, font_tiny(), kCrypto, "");  // order, in the top-right corner
    lv_obj_align(s_badge[i], LV_ALIGN_TOP_RIGHT, 3, 1);
    s_chip[i] = chip;
  }
  render();
}

}  // namespace coins
}  // namespace ui
