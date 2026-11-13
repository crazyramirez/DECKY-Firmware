#pragma once
// Design tokens (spec §6 palette) and shared styles.

#include <lvgl.h>

namespace ui {
namespace theme {

// Palette
constexpr uint32_t kBg = 0x090D15;
constexpr uint32_t kSurface = 0x171F2D;
constexpr uint32_t kSurfaceHi = 0x1F2A3C;  // pressed / nested surfaces
constexpr uint32_t kBorder = 0x304056;
constexpr uint32_t kText = 0xF5F8FF;
constexpr uint32_t kMuted = 0xA8B5C9;
constexpr uint32_t kCpu = 0x52D5E8;
constexpr uint32_t kGpu = 0xB99BFF;
constexpr uint32_t kRam = 0x68E5A0;
constexpr uint32_t kPower = 0xF4C66F;
constexpr uint32_t kWarning = 0xF59E0B;
constexpr uint32_t kCritical = 0xF87171;
constexpr uint32_t kOk = 0x68E5A0;

constexpr uint32_t kUp = 0x34D399;    // price up (Crypto)
constexpr uint32_t kDown = 0xF87171;  // price down
constexpr uint32_t kCrypto = 0xF7A531;

// Metrics (px). Landscape 1024x600 (7", ~170 dpi): navigation rail on the left, slim top bar,
// content area. Sizes are the 480x272 design scaled x1.5; the extra room goes to the content.
constexpr int kScreenW = 1024;
constexpr int kScreenH = 600;
constexpr int kNavW = 104;                      // left navigation rail
constexpr int kTopBarH = 52;
constexpr int kContentW = kScreenW - kNavW;     // 920
constexpr int kContentH = kScreenH - kTopBarH;  // 548
constexpr int kMargin = 14;
constexpr int kGap = 10;
constexpr int kRadius = 15;
constexpr int kTouchMin = 60;  // minimum height for primary touch targets (~9 mm at 170 dpi)

inline lv_color_t c(uint32_t rgb) { return lv_color_hex(rgb); }

void init();

const lv_font_t *font_micro();  // 15, text only (navigation-rail labels)
const lv_font_t *font_tiny();   // 18 (captions)
const lv_font_t *font_small();  // 21
const lv_font_t *font_body();   // 24
const lv_font_t *font_title();  // 30
const lv_font_t *font_value();  // 42
const lv_font_t *font_big();    // 60
const lv_font_t *font_clock();  // 78, digits / colon / dash / space / ° (Today temperature)
const lv_font_t *font_clock_xl();  // 150, digits / colon / dash / space / ° (screensaver, Today)
// Weather condition icons only (+ moon, bolt), a notch under the text sizes next to them so Today
// reads lighter: 92 (Today "now"), 34 (next hours), 26 (next days).
const lv_font_t *font_weather_xl();
const lv_font_t *font_weather_m();
const lv_font_t *font_weather_s();

lv_style_t *style_card();
lv_style_t *style_btn();
lv_style_t *style_btn_pressed();
lv_style_t *style_btn_disabled();

// Helpers
lv_obj_t *make_card(lv_obj_t *parent);
lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text = "");
lv_obj_t *make_button(lv_obj_t *parent, const char *text, uint32_t accent = kCpu);
lv_obj_t *make_bar(lv_obj_t *parent, uint32_t color, int h = 9);
void style_slider(lv_obj_t *slider, uint32_t color);
void style_chart(lv_obj_t *chart);
uint32_t level_color(int level);  // 0 ok, 1 warning, 2 critical

}  // namespace theme
}  // namespace ui
