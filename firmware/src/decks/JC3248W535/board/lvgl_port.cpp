#include "lvgl_port.h"

#include <Arduino.h>
#include <lvgl.h>

#include "board_pins.h"
#include "esp_heap_caps.h"
#include "ui/screen_cover.h"
#include "lcd_axs15231b.h"
#include "touch_axs15231b.h"

namespace lvgl_port {
namespace {

const char *TAG = "lvgl";

lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t s_disp_drv;
lv_indev_drv_t s_indev_drv;
uint16_t *s_fb = nullptr;
Stats s_stats;
bool s_touch_ok = false;
bool s_was_pressed = false;
bool s_screen_off = false;
bool s_swallow = false;  // finger that woke the screen is still down
void (*s_on_wake)() = nullptr;
lv_point_t s_last_point = {0, 0};

void log_cb(const char *buf) { Serial.print(buf); }

void flush_cb(lv_disp_drv_t *drv, const lv_area_t *, lv_color_t *) {
  if (lv_disp_flush_is_last(drv) && !s_screen_off) {
    if (lcd::push_frame(s_fb) == ESP_OK) {
      s_stats.frames++;
      s_stats.last_push_us = lcd::last_push_us();
    }
  }
  lv_disp_flush_ready(drv);
}

void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  touch::Point p;
  const bool down = s_touch_ok && touch::read(p) && p.pressed;
  if (s_screen_off || s_swallow) {
    if (down && s_screen_off) {
      s_screen_off = false;
      s_swallow = true;
      lv_disp_trig_activity(nullptr);
      if (s_on_wake) s_on_wake();
    }
    if (!down) s_swallow = false;
    s_was_pressed = down;
    data->state = LV_INDEV_STATE_RELEASED;
    data->point = s_last_point;
    return;
  }
  if (down) {
    s_last_point.x = static_cast<lv_coord_t>(p.x);
    s_last_point.y = static_cast<lv_coord_t>(p.y);
    data->state = LV_INDEV_STATE_PRESSED;
    if (!s_was_pressed) s_stats.touch_presses++;
    s_was_pressed = true;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
    s_was_pressed = false;
  }
  data->point = s_last_point;
}

}  // namespace

bool init() {
  if (lcd::init() != ESP_OK) return false;

  const size_t px = PCD_LCD_H_RES * PCD_LCD_V_RES;
  s_fb = static_cast<uint16_t *>(heap_caps_malloc(px * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!s_fb) {
    ESP_LOGE(TAG, "framebuffer alloc failed (%u B PSRAM)", (unsigned)(px * 2));
    return false;
  }
  memset(s_fb, 0, px * sizeof(uint16_t));
  lcd::push_frame(s_fb);  // clear power-on garbage before the backlight comes up

  s_touch_ok = touch::init();

  lv_init();  // must precede every other lv_* call
  lv_log_register_print_cb(log_cb);

  lv_disp_draw_buf_init(&s_draw_buf, s_fb, nullptr, px);
  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = PCD_LCD_H_RES;
  s_disp_drv.ver_res = PCD_LCD_V_RES;
  s_disp_drv.flush_cb = flush_cb;
  s_disp_drv.draw_buf = &s_draw_buf;
  s_disp_drv.direct_mode = 1;
  s_disp_drv.render_start_cb = ui::skip_covered_screen;  // no page drawn under full-screen sheets
  lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register(&s_indev_drv);

  ESP_LOGI(TAG, "LVGL %d.%d.%d ready, direct mode, fb %u B in PSRAM", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
           LVGL_VERSION_PATCH, (unsigned)(px * 2));
  return true;
}

void handle() { lv_timer_handler(); }

const Stats &stats() { return s_stats; }
bool touch_ok() { return s_touch_ok; }
const uint16_t *framebuffer() { return s_fb; }

void set_screen_off(bool off) {
  s_screen_off = off;
  if (!off) {
    lv_disp_trig_activity(nullptr);  // waking counts as activity (auto sleep)
    lv_obj_invalidate(lv_scr_act());  // push a fresh frame right away
  }
}
bool screen_off() { return s_screen_off; }
void set_wake_callback(void (*on_wake)()) { s_on_wake = on_wake; }
bool touch_pressed() { return s_was_pressed; }

}  // namespace lvgl_port
