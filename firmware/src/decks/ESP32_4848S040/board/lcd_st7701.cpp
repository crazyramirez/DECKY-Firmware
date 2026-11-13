#include "lcd_st7701.h"

#include <Arduino.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lcd {
namespace {

const char *TAG = "lcd";
esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t *s_fb = nullptr;
uint32_t s_last_push_us = 0;
SemaphoreHandle_t s_vsync = nullptr;  // given by the LCD ISR at the end of every frame

constexpr int kW = PCD_LCD_H_RES;
constexpr int kBandRows = 16;  // copy + write back in bands: the panel sees each band right away

// Timings (PCLK cycles / lines). The VSYNC interrupt fires at the end of the active area.
constexpr int kHsyncPw = 8, kHbp = 50, kHfp = 10;
constexpr int kVsyncPw = 8, kVbp = 20, kVfp = 10;
constexpr uint32_t kLineNs =
    static_cast<uint32_t>((PCD_LCD_H_RES + kHsyncPw + kHbp + kHfp) * 1000000000ULL / PCD_LCD_PCLK_HZ);  // ~45.7 us

uint32_t line_us(int lines) { return static_cast<uint32_t>(static_cast<uint64_t>(lines) * kLineNs / 1000); }
float s_bytes_per_us = 16.0f;  // measured copy speed (PSRAM to PSRAM, shared with the scan)

void wait_until(int64_t t_us) {
  int64_t left;
  while ((left = t_us - esp_timer_get_time()) > 2000) vTaskDelay(pdMS_TO_TICKS(left / 1000 - 1));
  while (esp_timer_get_time() < t_us) {
  }
}

IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_vsync, &woken);
  return woken == pdTRUE;
}

void write_back(int y_first, int y_last) {
  esp_cache_msync(s_fb + y_first * kW, static_cast<size_t>(y_last - y_first + 1) * kW * 2,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

// ---- 3-wire SPI (bit-banged; only the init sequence goes through it) ----
constexpr gpio_num_t kCs = static_cast<gpio_num_t>(PCD_PIN_LCD_CS);
constexpr gpio_num_t kSck = static_cast<gpio_num_t>(PCD_PIN_LCD_SCK);
constexpr gpio_num_t kSda = static_cast<gpio_num_t>(PCD_PIN_LCD_SDA);

void spi_word(bool data, uint8_t v) {  // 9 bits, MSB first: D/C (1 = parameter), then the byte
  const uint16_t w = (data ? 0x100 : 0) | v;
  for (int b = 8; b >= 0; b--) {
    gpio_set_level(kSck, 0);
    gpio_set_level(kSda, (w >> b) & 1);
    esp_rom_delay_us(1);
    gpio_set_level(kSck, 1);  // the panel samples on the rising edge
    esp_rom_delay_us(1);
  }
}

void spi_cmd(uint8_t cmd, const uint8_t *params, int n) {
  gpio_set_level(kCs, 0);
  esp_rom_delay_us(1);
  spi_word(false, cmd);
  for (int i = 0; i < n; i++) spi_word(true, params[i]);
  gpio_set_level(kSck, 0);
  esp_rom_delay_us(1);
  gpio_set_level(kCs, 1);
  esp_rom_delay_us(2);
}

// Vendor init ("type9", Guition ESP32-4848S040): {command, parameter count, parameters...};
// count kDelay = wait the next byte x 10 ms; kEnd ends the table.
constexpr uint8_t kDelay = 0xFE, kEnd = 0xFF;
const uint8_t kInit[] = {
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,  // command page 0 (BK0)
    0xC0, 2, 0x3B, 0x00,
    0xC1, 2, 0x0D, 0x02,
    0xC2, 2, 0x31, 0x05,
    0xCD, 1, 0x00,  // "changed by manufacture" (0x08 in the generic table)
    0xB0, 16, 0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18,  // + gamma
    0xB1, 16, 0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18,  // - gamma
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,  // command page 1 (BK1)
    0xB0, 1, 0x60,  // Vop 4.7375 V
    0xB1, 1, 0x32,  // VCOM
    0xB2, 1, 0x07,  // VGH 15 V
    0xB3, 1, 0x80,
    0xB5, 1, 0x49,  // VGL -10.17 V
    0xB7, 1, 0x85,
    0xB8, 1, 0x21,  // AVDD 6.6 V, AVCL -4.6 V
    0xC1, 1, 0x78,
    0xC2, 1, 0x78,
    0xE0, 3, 0x00, 0x1B, 0x02,
    0xE1, 11, 0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44,
    0xE2, 12, 0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00,
    0xE3, 4, 0x00, 0x00, 0x11, 0x11,
    0xE4, 2, 0x44, 0x44,
    0xE5, 16, 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0,
    0xE6, 4, 0x00, 0x00, 0x11, 0x11,
    0xE7, 2, 0x44, 0x44,
    0xE8, 16, 0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0,
    0xEB, 7, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40,
    0xEC, 2, 0x3C, 0x00,
    0xED, 16, 0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,  // BK3: VAP & VAN
    0xE5, 1, 0xE4,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,  // back to the user command set
    // 0x21 (inversion) stays off on this panel ("commented by manufacture")
    0x3A, 1, 0x60,  // interface pixel format (the RGB bus carries 16 bits; the panel wants 0x60 here)
    0x11, 0,        // sleep out
    0x00, kDelay, 12,
    0x29, 0,        // display on
    0x00, kEnd,
};

void panel_init_sequence() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << kCs) | (1ULL << kSck) | (1ULL << kSda);
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  gpio_set_level(kCs, 1);
  gpio_set_level(kSck, 0);
  gpio_set_level(kSda, 0);
  delay(20);
  for (size_t i = 0; i + 1 < sizeof(kInit);) {
    const uint8_t cmd = kInit[i], n = kInit[i + 1];
    if (n == kEnd) break;
    if (n == kDelay) {
      delay(kInit[i + 2] * 10);
      i += 3;
      continue;
    }
    spi_cmd(cmd, &kInit[i + 2], n);
    i += 2 + n;
  }
}

}  // namespace

esp_err_t init() {
  panel_init_sequence();

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = PCD_LCD_PCLK_HZ;
  cfg.timings.h_res = PCD_LCD_H_RES;
  cfg.timings.v_res = PCD_LCD_V_RES;
  cfg.timings.hsync_pulse_width = kHsyncPw;
  cfg.timings.hsync_back_porch = kHbp;
  cfg.timings.hsync_front_porch = kHfp;
  cfg.timings.vsync_pulse_width = kVsyncPw;
  cfg.timings.vsync_back_porch = kVbp;
  cfg.timings.vsync_front_porch = kVfp;
  cfg.timings.flags.hsync_idle_low = 0;
  cfg.timings.flags.vsync_idle_low = 0;
  cfg.timings.flags.de_idle_high = 0;
  cfg.timings.flags.pclk_active_neg = 0;
  cfg.timings.flags.pclk_idle_high = 0;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = 0;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = PCD_PIN_LCD_HSYNC;
  cfg.vsync_gpio_num = PCD_PIN_LCD_VSYNC;
  cfg.de_gpio_num = PCD_PIN_LCD_DE;
  cfg.pclk_gpio_num = PCD_PIN_LCD_PCLK;
  cfg.disp_gpio_num = -1;
  const int data[16] = {PCD_PIN_LCD_B0, PCD_PIN_LCD_B1, PCD_PIN_LCD_B2, PCD_PIN_LCD_B3, PCD_PIN_LCD_B4,
                        PCD_PIN_LCD_G0, PCD_PIN_LCD_G1, PCD_PIN_LCD_G2, PCD_PIN_LCD_G3, PCD_PIN_LCD_G4,
                        PCD_PIN_LCD_G5, PCD_PIN_LCD_R0, PCD_PIN_LCD_R1, PCD_PIN_LCD_R2, PCD_PIN_LCD_R3,
                        PCD_PIN_LCD_R4};
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
  cfg.flags.fb_in_psram = 1;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_lcd_new_rgb_panel: %s", esp_err_to_name(err));
    return err;
  }
  esp_lcd_panel_reset(s_panel);
  esp_lcd_panel_init(s_panel);
  void *fb = nullptr;
  err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb);
  if (err != ESP_OK || !fb) {
    ESP_LOGE(TAG, "no frame buffer: %s", esp_err_to_name(err));
    return err != ESP_OK ? err : ESP_ERR_NO_MEM;
  }
  s_fb = static_cast<uint16_t *>(fb);
  fill(0x0000);
  s_vsync = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = on_vsync;
  if (s_vsync) esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, nullptr);
  ESP_LOGI(TAG, "ST7701S %dx%d ready: RGB565 16-bit, PCLK %u Hz, frame buffer %p (PSRAM)", PCD_LCD_H_RES,
           PCD_LCD_V_RES, static_cast<unsigned>(PCD_LCD_PCLK_HZ), fb);
  return ESP_OK;
}

uint16_t *framebuffer() { return s_fb; }

void present(const uint16_t *src, Rect *rects, int n) {
  if (!s_fb || n <= 0) return;
  for (int i = 1; i < n; i++) {  // top-down (n <= LV_INV_BUF_SIZE)
    const Rect r = rects[i];
    int j = i - 1;
    for (; j >= 0 && rects[j].y1 > r.y1; j--) rects[j + 1] = rects[j];
    rects[j + 1] = r;
  }
  size_t bytes = 0;
  int top = rects[0].y1, bottom = rects[0].y2;
  for (int i = 0; i < n; i++) {
    bytes += static_cast<size_t>(rects[i].x2 - rects[i].x1 + 1) * (rects[i].y2 - rects[i].y1 + 1) * 2;
    if (rects[i].y2 > bottom) bottom = rects[i].y2;
  }
  // Never let the scan cross the copy (that would show half of an area for one frame, a tear):
  //  - fast copy: start at the end of a frame; it stays ahead of the next scan;
  //  - slow copy (big areas: slower than the scan): start once the scan has passed the top row, so
  //    the scan stays ahead and shows the whole change on the next frame.
  if (s_vsync) {
    xSemaphoreTake(s_vsync, 0);
    xSemaphoreTake(s_vsync, pdMS_TO_TICKS(50));
  }
  const int64_t t_frame = esp_timer_get_time();
  const uint32_t est_us = static_cast<uint32_t>(bytes / s_bytes_per_us);
  if (est_us >= line_us(kVbp + bottom)) wait_until(t_frame + line_us(kVfp + kVsyncPw + kVbp + top + 2));
  const uint32_t t0 = micros();
  for (int i = 0; i < n; i++) {
    const Rect &r = rects[i];
    const int x = r.x1, w = r.x2 - r.x1 + 1;
    for (int y0 = r.y1; y0 <= r.y2; y0 += kBandRows) {
      const int y1 = y0 + kBandRows - 1 < r.y2 ? y0 + kBandRows - 1 : r.y2;
      if (w == kW) {
        memcpy(s_fb + y0 * kW, src + y0 * kW, static_cast<size_t>(y1 - y0 + 1) * kW * 2);
      } else {
        for (int y = y0; y <= y1; y++) memcpy(s_fb + y * kW + x, src + y * kW + x, static_cast<size_t>(w) * 2);
      }
      write_back(y0, y1);
    }
  }
  const uint32_t took = micros() - t0;
  if (bytes >= 64 * 1024 && took > 0) s_bytes_per_us = 0.75f * s_bytes_per_us + 0.25f * bytes / took;
  s_last_push_us = took;
}

void fill(uint16_t color) {
  if (!s_fb) return;
  for (int i = 0; i < PCD_LCD_H_RES * PCD_LCD_V_RES; i++) s_fb[i] = color;
  write_back(0, PCD_LCD_V_RES - 1);
}

uint32_t last_push_us() { return s_last_push_us; }

}  // namespace lcd
