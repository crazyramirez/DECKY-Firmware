#pragma once
// Shared between UI modules only.

#include <lvgl.h>
#include <stddef.h>
#include <time.h>

#include "app/event_bus.h"
#include "ui/i18n.h"
#include "ui/theme/theme.h"

// Extra FontAwesome 5 glyphs baked into the pcd_font_* fonts (see tools/gen_fonts.ps1).
#define PCD_SYM_THERMO "\xEF\x8B\x89"   // U+F2C9 thermometer-half
#define PCD_SYM_LOCK "\xEF\x80\xA3"     // U+F023 lock
#define PCD_SYM_SLIDERS "\xEF\x87\x9E"  // U+F1DE sliders-h
#define PCD_SYM_GAUGE "\xEF\x8F\xBD"    // U+F3FD tachometer-alt
#define PCD_SYM_MOON "\xEF\x86\x86"     // U+F186 moon
// Weather (Today tab); the condition icons also exist at 40 px.
#define PCD_SYM_SUN "\xEF\x86\x85"             // U+F185 sun
#define PCD_SYM_CLOUD "\xEF\x83\x82"           // U+F0C2 cloud
#define PCD_SYM_CLOUD_SUN "\xEF\x9B\x84"       // U+F6C4 cloud-sun
#define PCD_SYM_CLOUD_MOON "\xEF\x9B\x83"      // U+F6C3 cloud-moon
#define PCD_SYM_CLOUD_RAIN "\xEF\x9C\xBD"      // U+F73D cloud-rain
#define PCD_SYM_CLOUD_HEAVY "\xEF\x9D\x80"     // U+F740 cloud-showers-heavy
#define PCD_SYM_CLOUD_SUN_RAIN "\xEF\x9D\x83"  // U+F743 cloud-sun-rain
#define PCD_SYM_CLOUD_MOON_RAIN "\xEF\x9C\xBC" // U+F73C cloud-moon-rain
#define PCD_SYM_SNOW "\xEF\x8B\x9C"            // U+F2DC snowflake
#define PCD_SYM_FOG "\xEF\x9D\x9F"             // U+F75F smog
#define PCD_SYM_BOLT "\xEF\x83\xA7"            // U+F0E7 bolt (= LV_SYMBOL_CHARGE)
#define PCD_SYM_WIND "\xEF\x9C\xAE"            // U+F72E wind
#define PCD_SYM_UMBRELLA "\xEF\x83\xA9"        // U+F0E9 umbrella
#define PCD_SYM_PIN "\xEF\x8F\x85"             // U+F3C5 map-marker-alt
#define PCD_SYM_SEARCH "\xEF\x80\x82"          // U+F002 search
#define PCD_SYM_TINT "\xEF\x81\x83"            // U+F043 tint (droplet)
#define PCD_SYM_CLOCK "\xEF\x80\x97"           // U+F017 clock
// Crypto tab
#define PCD_SYM_BITCOIN "\xEF\x8D\xB9"     // U+F379 bitcoin (brands)
#define PCD_SYM_COINS "\xEF\x94\x9E"       // U+F51E coins
#define PCD_SYM_CHART_LINE "\xEF\x88\x81"  // U+F201 chart-line
#define PCD_SYM_CHART_BAR "\xEF\x82\x80"   // U+F080 chart-bar (candles toggle)
#define PCD_SYM_CARET_UP "\xEF\x83\x98"    // U+F0D8 caret-up
#define PCD_SYM_CARET_DOWN "\xEF\x83\x97"  // U+F0D7 caret-down

namespace net {
struct PcEntry;
}

namespace ui {

enum class Page : uint8_t { Home, Thermals, Controls, Settings, DetailCpu, DetailGpu, Wifi, Pair, Today, Crypto, CryptoDetail };
constexpr int kPageCount = 11;

// Reorderable bottom-bar tabs (storage::Tab values): page, icon and name.
Page tab_page(uint8_t tab);
const char *tab_icon(uint8_t tab);
Str tab_label(uint8_t tab);

void navigate(Page p);
Page current_page();
Page first_tab_page();  // first visible bottom-bar tab (Settings if all are hidden)
void dev_scroll(int y);  // developer aid (USB console "scroll"), for screenshots of long pages

// Full-size page container (column flex, standard margins).
lv_obj_t *make_page(lv_obj_t *parent, bool scrollable);
lv_obj_t *make_chart(lv_obj_t *parent, int points, int y_max);

// Label text update that skips identical strings (each redraw costs a full-frame push).
void set_text(lv_obj_t *label, const char *text);
void set_color(lv_obj_t *obj, uint32_t rgb);
void set_bar(lv_obj_t *bar, float value, float max);  // NaN -> 0 and dimmed

// Formatting: missing values (NaN) render as "--".
void fmt_num(char *buf, size_t n, float v, int decimals);
void fmt_temp(char *buf, size_t n, float v);    // "68.2 °C"
void fmt_pct(char *buf, size_t n, float v);     // "42 %"
void fmt_ghz(char *buf, size_t n, float mhz);   // "5.40 GHz"
void fmt_watts(char *buf, size_t n, float w);   // "154 W"
void fmt_gib(char *buf, size_t n, float mib);   // "23.8 GB" (GiB, labelled GB for readability)
int temp_level(float temp_c, float warn, float crit);  // 0/1/2, -1 when missing

// Screen modules: create() builds inside the given content container, update() refreshes.
namespace dashboard {
lv_obj_t *create(lv_obj_t *parent);
void update();
}
namespace thermals {
lv_obj_t *create(lv_obj_t *parent);
void update();
}
namespace controls {
lv_obj_t *create(lv_obj_t *parent);
void update();
void on_telemetry();  // now playing + PC volume
void on_result(const app::CommandResult &r);
void on_art(const app::ArtFrame &art);  // takes ownership of art.data
const lv_img_dsc_t *cover();  // current album art (any size), nullptr when there is none
}
namespace settings {
lv_obj_t *create(lv_obj_t *parent);
void update();
void tick();
void on_tabs();  // tab order changed (also from the USB console)
void on_place();  // weather place / units changed
void on_crypto();  // crypto watch list / currency changed
void on_screensaver();  // screensaver settings changed (picker, USB console)
}
namespace wifi {
lv_obj_t *create(lv_obj_t *parent);
void update();   // status line; cheap, call every second while visible
void on_scan();  // net::scan_results() changed
void on_show();  // page shown (navigate): last list + a scan right away
void tick();     // every second while the page is current: re-scans every 15 s while in view
bool sheet_open();  // password sheet on screen
void open_sheet(const char *ssid, bool secure);  // password sheet (top layer); ssid nullptr = hidden network
void dev_open_connect(const char *ssid);  // developer aid: password modal (USB console)
}
namespace pair {
lv_obj_t *create(lv_obj_t *parent);
void update();
void on_show();       // starts an mDNS search
void on_discovery();  // net::discovery_results() changed
void on_pair_done(const app::PairOutcome &r);
void open_code_sheet(const net::PcEntry &pc);  // 6-digit code keypad for that PC (top layer)
void open_manual_sheet();                      // manual IP + port entry (top layer)
void dev_open_code();    // developer aid: code keypad (USB console)
void dev_open_manual();  // developer aid: manual IP modal
}
// First-run setup wizard (full screen, top layer): language, Wi-Fi, pairing, location, done.
namespace setup {
void open(int step = 0);  // 0..4; replaces an open wizard
void close();
bool active();
void rebuild();  // after a UI rebuild (language change): same step, new language
void tick();     // every second: connection state of the current step
void on_scan();
void on_discovery();
void on_pair_done(const app::PairOutcome &r);
void on_place();  // place / units / clock changed
}
namespace today {
lv_obj_t *create(lv_obj_t *parent);
void update();  // weather / units / place changed
void tick();    // clock (every second; repaints only when the minute changes)
// Shared with the screensaver: WMO weather code -> icon / colour / localized condition, long date.
const char *weather_icon(int code, bool day);
uint32_t weather_color(int code, bool day);
const char *weather_text(int code);
void format_date(const struct tm &t, char *out, size_t n);
}
namespace crypto_ui {
lv_obj_t *create(lv_obj_t *parent);
void on_show();   // page became visible
void on_data();   // crypto::get_market() changed (or failed)
void on_watch();  // watched coins / currency changed
void tick();      // every second: "updated 12 s ago"
}
namespace crypto_detail {
lv_obj_t *create(lv_obj_t *parent);
void show(int coin);  // COIN_CATALOG index; opens the page
void on_data();       // live price header
void on_chart();      // a chart arrived (or failed)
void tick();
}
namespace coins {
void open();  // coin picker sheet (top layer)
}
namespace screensaver {
void tick();          // every second: clock, data, crypto rotation, wallpaper cycle, burn-in drift
void on_data();       // telemetry / weather / media / crypto changed
void open_picker();   // wallpaper picker sheet (top layer, Settings)
void on_rebuild();    // UI rebuilt (language): close the screensaver and the picker
}
namespace place {
void open();        // city search sheet (top layer)
void on_results();  // net::place_results() changed
void dev_search(const char *query);  // developer aid: open the sheet and search (USB console)
}
// Weather helpers shared by Today and Settings.
void fmt_deg(char *buf, size_t n, float temp_c);  // "18°" in the user's unit, "--°" if missing

namespace detail {
lv_obj_t *create(lv_obj_t *parent);
void show(bool gpu);
void update();
}

// Chart helper: 60 s history series filled from a History60.
void chart_fill(lv_obj_t *chart, lv_chart_series_t *ser, const void *history /* telemetry::History60* */);

}  // namespace ui
