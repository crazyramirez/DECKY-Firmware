#include "theme.h"

namespace ui {
namespace theme {
namespace {
lv_style_t s_card, s_btn, s_btn_pr, s_btn_dis;
bool s_ready = false;
}  // namespace

const lv_font_t *font_tiny() { return &pcd_font_12; }
const lv_font_t *font_small() { return &pcd_font_14; }
const lv_font_t *font_body() { return &pcd_font_16; }
const lv_font_t *font_title() { return &pcd_font_20; }
const lv_font_t *font_value() { return &pcd_font_28; }
const lv_font_t *font_big() { return &pcd_font_40; }
const lv_font_t *font_clock() { return &pcd_font_72; }
const lv_font_t *font_clock_xl() { return &pcd_font_90; }

void init() {
  if (s_ready) return;
  s_ready = true;

  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t *th = lv_theme_default_init(disp, c(kCpu), c(kGpu), true, font_small());
  lv_disp_set_theme(disp, th);

  lv_style_init(&s_card);
  lv_style_set_bg_color(&s_card, c(kSurface));
  lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
  lv_style_set_radius(&s_card, kRadius);
  lv_style_set_border_color(&s_card, c(kBorder));
  lv_style_set_border_width(&s_card, 1);
  lv_style_set_border_opa(&s_card, LV_OPA_60);
  lv_style_set_pad_all(&s_card, 10);
  lv_style_set_shadow_width(&s_card, 0);

  lv_style_init(&s_btn);
  lv_style_set_bg_color(&s_btn, c(kSurfaceHi));
  lv_style_set_bg_opa(&s_btn, LV_OPA_COVER);
  lv_style_set_radius(&s_btn, 10);
  lv_style_set_border_color(&s_btn, c(kBorder));
  lv_style_set_border_width(&s_btn, 1);
  lv_style_set_shadow_width(&s_btn, 0);
  lv_style_set_text_color(&s_btn, c(kText));
  lv_style_set_text_font(&s_btn, font_body());

  lv_style_init(&s_btn_pr);
  lv_style_set_bg_color(&s_btn_pr, c(kBorder));

  lv_style_init(&s_btn_dis);
  lv_style_set_bg_opa(&s_btn_dis, LV_OPA_40);
  lv_style_set_text_opa(&s_btn_dis, LV_OPA_40);
  lv_style_set_border_opa(&s_btn_dis, LV_OPA_30);
}

lv_style_t *style_card() { return &s_card; }
lv_style_t *style_btn() { return &s_btn; }
lv_style_t *style_btn_pressed() { return &s_btn_pr; }
lv_style_t *style_btn_disabled() { return &s_btn_dis; }

lv_obj_t *make_card(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_add_style(o, &s_card, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c(color), 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, uint32_t accent) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &s_btn, 0);
  lv_obj_add_style(b, &s_btn_pr, LV_STATE_PRESSED);
  lv_obj_add_style(b, &s_btn_dis, LV_STATE_DISABLED);
  lv_obj_set_style_border_color(b, c(accent), LV_STATE_FOCUSED);
  lv_obj_set_height(b, kTouchMin + 4);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

lv_obj_t *make_bar(lv_obj_t *parent, uint32_t color, int h) {
  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_height(bar, h);
  lv_obj_set_style_bg_color(bar, c(kBorder), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, h / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, c(color), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_anim_time(bar, 0, 0);
  return bar;
}

void style_slider(lv_obj_t *slider, uint32_t color) {
  lv_obj_set_style_bg_color(slider, c(kBorder), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, c(color), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, c(kText), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
  lv_obj_set_ext_click_area(slider, 14);
}

void style_chart(lv_obj_t *chart) {
  lv_obj_set_style_bg_color(chart, c(kSurface), 0);
  lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_radius(chart, 0, 0);
  lv_obj_set_style_pad_all(chart, 0, 0);
  lv_obj_set_style_line_color(chart, c(kBorder), LV_PART_MAIN);  // division lines
  lv_obj_set_style_line_opa(chart, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);  // no point markers
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(chart, 3, 0);
  lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
}

uint32_t level_color(int level) {
  switch (level) {
    case 2: return kCritical;
    case 1: return kWarning;
    default: return kOk;
  }
}

}  // namespace theme
}  // namespace ui
