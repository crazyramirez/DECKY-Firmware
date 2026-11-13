// Hardware validation screen (Guition ESP32-4848S040: ST7701S 480x480 RGB + GT911).
//
// What it shows / logs:
//   * Title "DECKY - HARDWARE TEST"
//   * Pure colour bars R/G/B/W/K (+ labels) to detect swapped bytes / BGR / inversion
//   * Four corner targets: touch each one; it turns green if the touch lands on it
//     (validates touch orientation and mapping), plus a live crosshair and x/y readout
//   * Brightness slider (PWM backlight 5..100 %)
//   * Async Wi-Fi scan running while LVGL renders (Wi-Fi + display coexistence)
//   * Serial: chip/flash/PSRAM info, I2C scan, heap/PSRAM every 2 s, touch stats, frame timing

#include "hwtest/hw_test.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "board/backlight.h"
#include "board/lvgl_port.h"
#include "board/touch_gt911.h"
#include "board_pins.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"

namespace hwtest {
namespace {

const char *TAG = "hwtest";

lv_obj_t *s_info = nullptr;
lv_obj_t *s_touch_lbl = nullptr;
lv_obj_t *s_cross_h = nullptr;
lv_obj_t *s_cross_v = nullptr;
lv_obj_t *s_corners[4] = {};
lv_obj_t *s_wifi_lbl = nullptr;
uint32_t s_last_log = 0;
uint32_t s_last_info = 0;
uint32_t s_last_frames = 0;
uint32_t s_wifi_scan_started = 0;
int s_wifi_found = -1;

void log_memory(const char *when) {
  ESP_LOGI(TAG, "[%s] heap internal free=%u min=%u largest=%u | psram free=%u largest=%u", when,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

void log_chip() {
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  uint32_t flash = 0;
  esp_flash_get_size(nullptr, &flash);
  ESP_LOGI(TAG, "chip ESP32-S3 rev %d.%d, %d cores, flash %u MB, PSRAM %u KB (found=%d), CPU %u MHz",
           ci.revision / 100, ci.revision % 100, ci.cores, (unsigned)(flash >> 20),
           (unsigned)(ESP.getPsramSize() / 1024), psramFound() ? 1 : 0, (unsigned)getCpuFrequencyMhz());
  ESP_LOGI(TAG, "SDK %s, Arduino %d.%d.%d", ESP.getSdkVersion(), ESP_ARDUINO_VERSION_MAJOR,
           ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  ESP_LOGI(TAG, "LCD ST7701S %dx%d, RGB565 PCLK %d MHz", PCD_LCD_H_RES, PCD_LCD_V_RES, PCD_LCD_PCLK_HZ / 1000000);
  uint16_t tx = 0, ty = 0;
  touch::config_resolution(tx, ty);
  ESP_LOGI(TAG, "touch GT911 addr 0x%02X, configured %ux%u", touch::address(), tx, ty);
}

void log_i2c_scan() {
  uint8_t found[16];
  const int n = touch::scan_bus(found, 16);
  char line[128] = "";
  for (int i = 0; i < n && i < 16; i++) {
    char tmp[8];
    snprintf(tmp, sizeof(tmp), " 0x%02X", found[i]);
    strncat(line, tmp, sizeof(line) - strlen(line) - 1);
  }
  ESP_LOGI(TAG, "I2C scan (SDA=%d SCL=%d): %d device(s):%s", PCD_PIN_TOUCH_SDA, PCD_PIN_TOUCH_SCL, n, line);
}

lv_obj_t *make_bar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, const char *txt, uint32_t txt_rgb) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(0x606060), 0);
  lv_obj_set_style_border_width(o, rgb == 0 ? 1 : 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *l = lv_label_create(o);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, lv_color_hex(txt_rgb), 0);
  lv_obj_center(l);
  return o;
}

void corner_cb(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  lv_obj_set_style_bg_color(o, lv_color_hex(0x22C55E), 0);
  const int idx = (int)(intptr_t)lv_event_get_user_data(e);
  static const char *names[4] = {"top-left", "top-right", "bottom-left", "bottom-right"};
  ESP_LOGI(TAG, "corner target hit: %s", names[idx]);
}

void slider_cb(lv_event_t *e) {
  lv_obj_t *s = lv_event_get_target(e);
  backlight::set(static_cast<uint8_t>(lv_slider_get_value(s)));
}

void screen_touch_cb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  lv_obj_set_pos(s_cross_h, 0, p.y);
  lv_obj_set_pos(s_cross_v, p.x, 0);
  lv_label_set_text_fmt(s_touch_lbl, "touch x=%d y=%d  presses=%u", p.x, p.y,
                        (unsigned)lvgl_port::stats().touch_presses);
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
    const uint8_t *r = touch::last_raw();
    ESP_LOGI(TAG, "press x=%d y=%d raw=%02X %02X %02X %02X %02X %02X %02X %02X", p.x, p.y, r[0], r[1], r[2], r[3],
             r[4], r[5], r[6], r[7]);
  }
}

void build_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x090D15), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, screen_touch_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(scr, screen_touch_cb, LV_EVENT_PRESSING, nullptr);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "DECKY - HARDWARE TEST");
  lv_obj_set_style_text_color(title, lv_color_hex(0x52D5E8), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  // Colour bars: if RED shows blue or near-black, byte order / RGB order is wrong.
  const int bw = 76, bh = 40, by = 46, bx0 = 50;
  make_bar(scr, bx0 + 0 * bw, by, bw, bh, 0xFF0000, "RED", 0xFFFFFF);
  make_bar(scr, bx0 + 1 * bw, by, bw, bh, 0x00FF00, "GREEN", 0x000000);
  make_bar(scr, bx0 + 2 * bw, by, bw, bh, 0x0000FF, "BLUE", 0xFFFFFF);
  make_bar(scr, bx0 + 3 * bw, by, bw, bh, 0xFFFFFF, "WHITE", 0x000000);
  make_bar(scr, bx0 + 4 * bw, by, bw, bh, 0x000000, "BLACK", 0xFFFFFF);
  make_bar(scr, bx0, by + bh + 4, 5 * bw, 18, 0xF4C66F, "amber #F4C66F", 0x000000);

  // Corner touch targets (40x40).
  const int cs = 40;
  const lv_coord_t cx[4] = {0, PCD_LCD_H_RES - cs, 0, PCD_LCD_H_RES - cs};
  const lv_coord_t cy[4] = {0, 0, PCD_LCD_V_RES - cs, PCD_LCD_V_RES - cs};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, cx[i], cy[i]);
    lv_obj_set_size(c, cs, cs);
    lv_obj_set_style_bg_color(c, lv_color_hex(0xF87171), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, corner_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    s_corners[i] = c;
  }

  lv_obj_t *bl = lv_label_create(scr);
  lv_label_set_text(bl, "Backlight");
  lv_obj_set_style_text_color(bl, lv_color_hex(0xA8B5C9), 0);
  lv_obj_set_pos(bl, 50, 116);
  lv_obj_t *slider = lv_slider_create(scr);
  lv_obj_set_size(slider, 270, 14);
  lv_obj_set_pos(slider, 150, 118);
  lv_slider_set_range(slider, 5, 100);
  lv_slider_set_value(slider, backlight::get(), LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  s_touch_lbl = lv_label_create(scr);
  lv_label_set_text(s_touch_lbl, "touch: tap anywhere / the 4 red corners");
  lv_obj_set_style_text_color(s_touch_lbl, lv_color_hex(0xF5F8FF), 0);
  lv_obj_set_pos(s_touch_lbl, 50, 144);

  s_wifi_lbl = lv_label_create(scr);
  lv_label_set_text(s_wifi_lbl, "Wi-Fi: scanning...");
  lv_obj_set_style_text_color(s_wifi_lbl, lv_color_hex(0x68E5A0), 0);
  lv_obj_set_pos(s_wifi_lbl, 50, 166);

  s_info = lv_label_create(scr);
  lv_obj_set_width(s_info, 380);
  lv_obj_set_style_text_color(s_info, lv_color_hex(0xA8B5C9), 0);
  lv_obj_set_style_text_font(s_info, &lv_font_montserrat_12, 0);
  lv_obj_set_pos(s_info, 50, 190);
  lv_label_set_text(s_info, "");

  // Crosshair (non-clickable so it never steals touches).
  s_cross_h = lv_obj_create(scr);
  lv_obj_remove_style_all(s_cross_h);
  lv_obj_set_size(s_cross_h, PCD_LCD_H_RES, 1);
  lv_obj_set_style_bg_color(s_cross_h, lv_color_hex(0xB99BFF), 0);
  lv_obj_set_style_bg_opa(s_cross_h, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_cross_h, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_cross_h, 0, -10);
  s_cross_v = lv_obj_create(scr);
  lv_obj_remove_style_all(s_cross_v);
  lv_obj_set_size(s_cross_v, 1, PCD_LCD_V_RES);
  lv_obj_set_style_bg_color(s_cross_v, lv_color_hex(0xB99BFF), 0);
  lv_obj_set_style_bg_opa(s_cross_v, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_cross_v, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_cross_v, -10, 0);
}

bool s_scanning = false;

void start_wifi_scan() {
  WiFi.scanNetworks(true /*async*/, false /*hidden*/);
  s_wifi_scan_started = millis();
  s_scanning = true;
}

void poll_wifi() {
  // Re-scan every 15 s to keep the radio busy while LVGL renders.
  if (!s_scanning) {
    if (millis() - s_wifi_scan_started > 15000) start_wifi_scan();
    return;
  }
  const int r = WiFi.scanComplete();
  if (r == WIFI_SCAN_RUNNING) return;
  s_scanning = false;  // after scanDelete() scanComplete() reports -2 ("no scan"), not a failure
  if (r >= 0) {
    s_wifi_found = r;
    int best = -127;
    for (int i = 0; i < r; i++) best = max(best, (int)WiFi.RSSI(i));
    ESP_LOGI(TAG, "Wi-Fi scan: %d network(s), best RSSI %d dBm, %u ms", r, r ? best : 0,
             (unsigned)(millis() - s_wifi_scan_started));
    lv_label_set_text_fmt(s_wifi_lbl, "Wi-Fi: %d networks, best %d dBm", r, r ? best : 0);
    WiFi.scanDelete();
  } else {
    ESP_LOGW(TAG, "Wi-Fi scan failed (%d)", r);
    lv_label_set_text(s_wifi_lbl, "Wi-Fi: scan FAILED");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);  // give the USB-CDC host a moment so boot logs are not lost
  ESP_LOGI(TAG, "=== DECKY: hardware test (ESP32-4848S040) ===");
  log_chip();
  log_memory("boot");

  backlight::init(0);
  if (!lvgl_port::init()) {
    ESP_LOGE(TAG, "display init FAILED - halting");
    backlight::init(100);
    while (true) delay(1000);
  }
  log_i2c_scan();
  build_ui();
  lv_timer_handler();  // render first frame before lighting the panel
  backlight::set(80);
  log_memory("ui ready");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  start_wifi_scan();
}

void loop() {
  lvgl_port::handle();
  poll_wifi();

  const uint32_t now = millis();
  if (now - s_last_info >= 1000) {
    const auto &st = lvgl_port::stats();
    const uint32_t fps = st.frames - s_last_frames;
    s_last_frames = st.frames;
    s_last_info = now;
    const auto &ts = touch::stats();
    lv_label_set_text_fmt(s_info,
                          "frames/s %u  push %u us\n"
                          "heap int %u KB (min %u)  psram %u KB\n"
                          "touch %s  reads %u  i2c err %u  rej %u\n"
                          "uptime %u s",
                          (unsigned)fps, (unsigned)st.last_push_us,
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                          lvgl_port::touch_ok() ? "OK" : "NO-ACK", (unsigned)ts.reads, (unsigned)ts.i2c_errors,
                          (unsigned)ts.rejected, (unsigned)(now / 1000));
  }
  if (now - s_last_log >= 5000) {
    s_last_log = now;
    // Boot logs are often lost while the USB-Serial/JTAG port re-enumerates: repeat the summary.
    static uint32_t n = 0;
    if (n++ % 6 == 0) {
      log_chip();
      log_i2c_scan();
    }
    log_memory("periodic");
    const auto &st = lvgl_port::stats();
    const auto &ts = touch::stats();
    ESP_LOGI(TAG, "frames=%u last_push=%u us touch_reads=%u i2c_err=%u rejected=%u presses=%u wifi_found=%d",
             (unsigned)st.frames, (unsigned)st.last_push_us, (unsigned)ts.reads, (unsigned)ts.i2c_errors,
             (unsigned)ts.rejected, (unsigned)st.touch_presses, s_wifi_found);
  }
  delay(5);
}

}  // namespace hwtest
