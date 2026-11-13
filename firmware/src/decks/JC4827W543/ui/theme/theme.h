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

// Metrics (px). Landscape 480x272: navigation rail on the left, slim top bar, content area.
constexpr int kScreenW = 480;
constexpr int kScreenH = 272;
constexpr int kNavW = 64;                       // left navigation rail
constexpr int kTopBarH = 30;
constexpr int kContentW = kScreenW - kNavW;     // 416
constexpr int kContentH = kScreenH - kTopBarH;  // 242
constexpr int kMargin = 8;
constexpr int kGap = 6;
constexpr int kRadius = 10;
constexpr int kTouchMin = 40;  // minimum height for primary touch targets (~8 mm at 128 dpi)

inline lv_color_t c(uint32_t rgb) { return lv_color_hex(rgb); }

void init();

const lv_font_t *font_micro();  // 10, text only (navigation-rail labels)
const lv_font_t *font_tiny();   // 12, text only (captions)
const lv_font_t *font_small();  // 14
const lv_font_t *font_body();   // 16
const lv_font_t *font_title();  // 20
const lv_font_t *font_value();  // 28
const lv_font_t *font_big();    // 40
const lv_font_t *font_clock();  // 52, digits / colon / space only (Today clock)
const lv_font_t *font_clock_xl();  // 84, digits / colon / space only (screensaver clock)

lv_style_t *style_card();
lv_style_t *style_btn();
lv_style_t *style_btn_pressed();
lv_style_t *style_btn_disabled();

// Helpers
lv_obj_t *make_card(lv_obj_t *parent);
lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text = "");
lv_obj_t *make_button(lv_obj_t *parent, const char *text, uint32_t accent = kCpu);
lv_obj_t *make_bar(lv_obj_t *parent, uint32_t color, int h = 6);
void style_slider(lv_obj_t *slider, uint32_t color);
void style_chart(lv_obj_t *chart);
uint32_t level_color(int level);  // 0 ok, 1 warning, 2 critical

}  // namespace theme
}  // namespace ui
