#include "lvgl_port.h"

#include <Arduino.h>
#include <lvgl.h>

#include "board_pins.h"
#include "esp_heap_caps.h"
#include "ui/screen_cover.h"
#include "lcd_jd9165.h"
#include "board/touch_gt911.h"

namespace lvgl_port {
namespace {

const char *TAG = "lvgl";
constexpr uint32_t kFlushTimeoutMs = 100;

lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t s_disp_drv;
lv_indev_drv_t s_indev_drv;
Stats s_stats;
bool s_touch_ok = false;
bool s_was_pressed = false;
bool s_screen_off = false;
bool s_swallow = false;  // finger that woke the screen is still down
bool s_copying = false;  // a band is being copied by DMA2D (its buffer is not free yet)
void (*s_on_wake)() = nullptr;
lv_point_t s_last_point = {0, 0};

void log_cb(const char *buf) { ESP_LOGW(TAG, "%s", buf); }

// Band rendered: hand it to DMA2D. LVGL renders the next band into the other buffer meanwhile and
// calls wait_cb() before it needs this one again.
void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
  if (!s_screen_off && lcd::draw(area->x1, area->y1, area->x2 + 1, area->y2 + 1, px) == ESP_OK) {
    s_copying = true;
    if (lv_disp_flush_is_last(drv)) s_stats.frames++;
    return;  // wait_cb() completes the flush
  }
  lv_disp_flush_ready(drv);
}

void monitor_cb(lv_disp_drv_t *, uint32_t ms, uint32_t px) {
  s_stats.last_refr_ms = ms;
  s_stats.last_refr_px = px;
  if (ms > s_stats.max_refr_ms) s_stats.max_refr_ms = ms;
}

void wait_cb(lv_disp_drv_t *drv) {
  if (s_copying) {
    if (!lcd::wait_done(kFlushTimeoutMs)) {
      s_stats.flush_timeouts++;
      ESP_LOGW(TAG, "DMA2D copy timeout");
    }
    s_copying = false;
    s_stats.last_push_us = lcd::last_draw_us();
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
      lv_obj_invalidate(lv_scr_act());  // the frame was not updated while the screen was off
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

void *alloc_band(size_t bytes) {
  // Internal RAM renders fastest; PSRAM is the fallback (still DMA2D-capable).
  void *p = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!p) {
    ESP_LOGW(TAG, "band buffer -> PSRAM");
    p = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  return p;
}

}  // namespace

bool init() {
  if (lcd::init() != ESP_OK) return false;

  const uint32_t px = static_cast<uint32_t>(PCD_LCD_H_RES) * PCD_LVGL_BUF_LINES;
  void *b1 = alloc_band(px * sizeof(lv_color_t));
  void *b2 = alloc_band(px * sizeof(lv_color_t));
  if (!b1 || !b2) {
    ESP_LOGE(TAG, "band buffers alloc failed (2 x %u B)", static_cast<unsigned>(px * sizeof(lv_color_t)));
    return false;
  }

  s_touch_ok = touch::init();

  lv_init();  // must precede every other lv_* call
  lv_log_register_print_cb(log_cb);

  lv_disp_draw_buf_init(&s_draw_buf, b1, b2, px);
  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = PCD_LCD_H_RES;
  s_disp_drv.ver_res = PCD_LCD_V_RES;
  s_disp_drv.flush_cb = flush_cb;
  s_disp_drv.wait_cb = wait_cb;
  s_disp_drv.monitor_cb = monitor_cb;
  s_disp_drv.draw_buf = &s_draw_buf;
  s_disp_drv.render_start_cb = ui::skip_covered_screen;  // no page drawn under full-screen sheets
  lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = touch_read_cb;
  // Finger-friendly thresholds for a 7" panel at ~170 dpi (the defaults suit ~130 dpi).
  s_indev_drv.scroll_limit = 14;
  s_indev_drv.gesture_min_velocity = 4;
  s_indev_drv.gesture_limit = 70;
  lv_indev_drv_register(&s_indev_drv);

  ESP_LOGI(TAG, "LVGL %d.%d.%d ready, partial mode, 2 x %u-line bands", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
           LVGL_VERSION_PATCH, static_cast<unsigned>(PCD_LVGL_BUF_LINES));
  return true;
}

void handle() { lv_timer_handler(); }

const Stats &stats() { return s_stats; }
void reset_max_refr() { s_stats.max_refr_ms = 0; }
bool touch_ok() { return s_touch_ok; }

const uint16_t *framebuffer() {
  if (s_copying) {  // let the last band land first
    lcd::wait_done(kFlushTimeoutMs);
    s_copying = false;
    lv_disp_flush_ready(&s_disp_drv);
  }
  lcd::sync_framebuffer_for_read();
  return lcd::framebuffer();
}

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
