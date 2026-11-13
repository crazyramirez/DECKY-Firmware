#include "lcd_jd9165.h"

#include <string.h>

#include "board_pins.h"
#include "esp_cache.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern "C" {
#include "esp_lcd_jd9165.h"
}

namespace lcd {
namespace {

const char *TAG = "lcd";
constexpr size_t kFbBytes = static_cast<size_t>(PCD_LCD_H_RES) * PCD_LCD_V_RES * 2;

esp_lcd_panel_handle_t s_panel = nullptr;
esp_lcd_panel_io_handle_t s_io = nullptr;
SemaphoreHandle_t s_done = nullptr;
uint16_t *s_fb = nullptr;
int64_t s_draw_start = 0;
volatile uint32_t s_last_draw_us = 0;

// DMA2D finished copying a rectangle into the frame buffer (ISR context).
bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_done, &woken);
  return woken == pdTRUE;
}

}  // namespace

esp_err_t init() {
  // 1. MIPI D-PHY supply.
  esp_ldo_channel_handle_t ldo = nullptr;
  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id = PCD_DSI_PHY_LDO_CHAN;
  ldo_cfg.voltage_mv = PCD_DSI_PHY_LDO_MV;
  esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &ldo);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LDO %d for the DSI PHY failed: %s", PCD_DSI_PHY_LDO_CHAN, esp_err_to_name(err));
    return err;
  }

  // 2. DSI bus + DBI command interface.
  esp_lcd_dsi_bus_handle_t bus = nullptr;
  esp_lcd_dsi_bus_config_t bus_cfg = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
  bus_cfg.num_data_lanes = PCD_DSI_LANES;
  bus_cfg.lane_bit_rate_mbps = PCD_DSI_LANE_MBPS;
  err = esp_lcd_new_dsi_bus(&bus_cfg, &bus);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "DSI bus init failed: %s", esp_err_to_name(err));
    return err;
  }
  esp_lcd_dbi_io_config_t dbi_cfg = JD9165_PANEL_IO_DBI_CONFIG();
  err = esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &s_io);
  if (err != ESP_OK) return err;

  // 3. DPI video panel: one frame buffer in PSRAM, rectangles copied in by DMA2D.
  esp_lcd_dpi_panel_config_t dpi_cfg = JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
  dpi_cfg.dpi_clock_freq_mhz = PCD_DPI_CLOCK_MHZ;
  dpi_cfg.num_fbs = 1;
  dpi_cfg.flags.use_dma2d = true;

  jd9165_vendor_config_t vendor = {};
  vendor.mipi_config.dsi_bus = bus;
  vendor.mipi_config.dpi_config = &dpi_cfg;

  esp_lcd_panel_dev_config_t dev_cfg = {};
  dev_cfg.reset_gpio_num = PCD_PIN_LCD_RST;
  dev_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  dev_cfg.bits_per_pixel = 16;
  dev_cfg.vendor_config = &vendor;

  err = esp_lcd_new_panel_jd9165(s_io, &dev_cfg, &s_panel);
  if (err == ESP_OK) err = esp_lcd_panel_reset(s_panel);
  if (err == ESP_OK) err = esp_lcd_panel_init(s_panel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "JD9165 init failed: %s", esp_err_to_name(err));
    return err;
  }

  s_done = xSemaphoreCreateBinary();
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = on_color_trans_done;
  err = esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, nullptr);
  if (err != ESP_OK) return err;

  // 4. Start from a black frame (power-on garbage would show when the backlight comes up).
  void *fb = nullptr;
  if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &fb) == ESP_OK && fb) {
    s_fb = static_cast<uint16_t *>(fb);
    memset(s_fb, 0, kFbBytes);
    esp_cache_msync(s_fb, kFbBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
  ESP_LOGI(TAG, "JD9165 %dx%d ready: DSI %d lanes @ %d Mbps, DPI %d MHz, frame buffer %p", PCD_LCD_H_RES, PCD_LCD_V_RES,
           PCD_DSI_LANES, PCD_DSI_LANE_MBPS, PCD_DPI_CLOCK_MHZ, s_fb);
  return ESP_OK;
}

esp_err_t draw(int x1, int y1, int x2, int y2, const void *pixels) {
  if (!s_panel) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_done, 0);  // drop a stale completion
  s_draw_start = esp_timer_get_time();
  return esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, pixels);
}

bool wait_done(uint32_t timeout_ms) {
  if (!s_done) return false;
  const bool ok = xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  if (ok) s_last_draw_us = static_cast<uint32_t>(esp_timer_get_time() - s_draw_start);
  return ok;
}

uint16_t *framebuffer() { return s_fb; }

void sync_framebuffer_for_read() {
  if (s_fb) esp_cache_msync(s_fb, kFbBytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

esp_err_t set_display_on(bool on) { return s_panel ? esp_lcd_panel_disp_on_off(s_panel, on) : ESP_ERR_INVALID_STATE; }

uint32_t last_draw_us() { return s_last_draw_us; }

}  // namespace lcd
