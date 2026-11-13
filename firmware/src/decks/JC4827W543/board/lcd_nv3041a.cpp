// NV3041A QSPI panel driver for the Guition JC4827W543.
//
// Init table and QSPI framing from Arduino_GFX 1.4.4 (BSD license), as shipped in the vendor
// package for this board (Arduino_NV3041A.h nv3041a_init_operations + Arduino_ESP32QSPI).
// Re-written on top of the ESP-IDF spi_master driver so we own the flush path (DMA bounce
// buffers, no Arduino_GFX dependency).

#include "lcd_nv3041a.h"

#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef PCD_LCD_SPLIT_PARAMS
#define PCD_LCD_SPLIT_PARAMS 1
#endif

namespace lcd {
namespace {

const char *TAG = "lcd";

constexpr uint8_t kOpCmd = 0x02;    // command (+ parameters on one line)
constexpr uint8_t kOpColor = 0x32;  // memory write, data on 4 lines
constexpr uint32_t kAddrRamWrC = 0x003C00;
constexpr uint8_t kCmdSwReset = 0x01;
constexpr uint8_t kCmdSlpIn = 0x10;
constexpr uint8_t kCmdSlpOut = 0x11;
constexpr uint8_t kCmdInvOn = 0x21;
constexpr uint8_t kCmdDispOff = 0x28;
constexpr uint8_t kCmdDispOn = 0x29;
constexpr uint8_t kCmdCaset = 0x2A;
constexpr uint8_t kCmdRaset = 0x2B;
constexpr uint8_t kCmdRamWr = 0x2C;

constexpr int kBandRows = 16;  // 272 / 16 = 17 bands of 15 KB
constexpr size_t kBandPixels = PCD_LCD_H_RES * kBandRows;
constexpr size_t kBandBytes = kBandPixels * sizeof(uint16_t);
static_assert(PCD_LCD_V_RES % kBandRows == 0, "band rows must divide the panel height");

// {register, value} pairs, each sent as one 0x02 transaction with a single parameter byte.
const uint8_t kInit[][2] = {
    {0xFF, 0xA5}, {0x36, 0xC0},  // MADCTL MX|MY: native landscape, RGB
    {0x3A, 0x01},                // 565
    {0x41, 0x03},                // 16-bit bus
    {0x44, 0x15}, {0x45, 0x15}, {0x7D, 0x03}, {0xC1, 0xAB}, {0xC2, 0x17}, {0xC3, 0x10}, {0xC6, 0x3A},
    {0xC7, 0x25}, {0xC8, 0x11}, {0x7A, 0x49}, {0x6F, 0x2F}, {0x78, 0x4B}, {0xC9, 0x00}, {0x67, 0x33},
    // gate
    {0x51, 0x4B}, {0x52, 0x7C}, {0x53, 0x1C}, {0x54, 0x77},
    // source
    {0x46, 0x0A}, {0x47, 0x2A}, {0x48, 0x0A}, {0x49, 0x1A}, {0x56, 0x43}, {0x57, 0x42}, {0x58, 0x3C},
    {0x59, 0x64}, {0x5A, 0x41}, {0x5B, 0x3C}, {0x5C, 0x02}, {0x5D, 0x3C}, {0x5E, 0x1F}, {0x60, 0x80},
    {0x61, 0x3F}, {0x62, 0x21}, {0x63, 0x07}, {0x64, 0xE0}, {0x65, 0x01}, {0xCA, 0x20}, {0xCB, 0x52},
    {0xCC, 0x10}, {0xCD, 0x42}, {0xD0, 0x20}, {0xD1, 0x52}, {0xD2, 0x10}, {0xD3, 0x42}, {0xD4, 0x0A},
    {0xD5, 0x32},
    // gamma
    {0x80, 0x04}, {0xA0, 0x00}, {0x81, 0x07}, {0xA1, 0x05}, {0x82, 0x06}, {0xA2, 0x04}, {0x86, 0x2C},
    {0xA6, 0x2A}, {0x87, 0x46}, {0xA7, 0x44}, {0x83, 0x39}, {0xA3, 0x39}, {0x84, 0x3A}, {0xA4, 0x3A},
    {0x85, 0x3F}, {0xA5, 0x3F}, {0x88, 0x08}, {0xA8, 0x08}, {0x89, 0x0F}, {0xA9, 0x0F}, {0x8A, 0x17},
    {0xAA, 0x17}, {0x8B, 0x10}, {0xAB, 0x10}, {0x8C, 0x16}, {0xAC, 0x16}, {0x8D, 0x14}, {0xAD, 0x14},
    {0x8E, 0x11}, {0xAE, 0x11}, {0x8F, 0x14}, {0xAF, 0x14}, {0x90, 0x06}, {0xB0, 0x06}, {0x91, 0x0F},
    {0xB1, 0x0F}, {0x92, 0x16}, {0xB2, 0x16},
    {0xFF, 0x00},
};

spi_device_handle_t s_dev = nullptr;
uint16_t *s_bounce[2] = {nullptr, nullptr};
spi_transaction_ext_t s_tx[2];
uint32_t s_last_push_us = 0;

inline void cs(bool active) { gpio_set_level(static_cast<gpio_num_t>(PCD_PIN_LCD_CS), active ? 0 : 1); }

// One complete transaction (own CS cycle), polling: commands and parameters are tiny.
esp_err_t tx_cmd(uint8_t cmd, const uint8_t *data, size_t n) {
  spi_transaction_ext_t t = {};
  t.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
  t.base.cmd = kOpCmd;
  t.base.addr = static_cast<uint32_t>(cmd) << 8;
  if (n && n <= 4) {
    t.base.flags |= SPI_TRANS_USE_TXDATA;
    memcpy(t.base.tx_data, data, n);
  } else {
    t.base.tx_buffer = data;
  }
  t.base.length = n * 8;
  cs(true);
  const esp_err_t err = spi_device_polling_transmit(s_dev, &t.base);
  cs(false);
  return err;
}

// One parameter byte as a memory-write transaction (Arduino_GFX Arduino_ESP32QSPI::write).
esp_err_t tx_param_byte(uint8_t b) {
  spi_transaction_ext_t t = {};
  t.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MODE_QIO;
  t.base.cmd = kOpColor;
  t.base.addr = kAddrRamWrC;
  t.base.tx_data[0] = b;
  t.base.length = 8;
  cs(true);
  const esp_err_t err = spi_device_polling_transmit(s_dev, &t.base);
  cs(false);
  return err;
}

esp_err_t tx_window_param(uint8_t cmd, uint16_t a, uint16_t b) {
  const uint8_t d[4] = {static_cast<uint8_t>(a >> 8), static_cast<uint8_t>(a), static_cast<uint8_t>(b >> 8),
                        static_cast<uint8_t>(b)};
#if PCD_LCD_SPLIT_PARAMS
  ESP_RETURN_ON_ERROR(tx_cmd(cmd, nullptr, 0), TAG, "window cmd");
  for (uint8_t v : d) ESP_RETURN_ON_ERROR(tx_param_byte(v), TAG, "window param");
  return ESP_OK;
#else
  return tx_cmd(cmd, d, sizeof(d));
#endif
}

}  // namespace

esp_err_t init() {
  ESP_LOGI(TAG, "init: NV3041A QSPI %d MHz, split params %d", PCD_LCD_PCLK_HZ / 1000000, PCD_LCD_SPLIT_PARAMS);
  for (auto &b : s_bounce) {
    b = static_cast<uint16_t *>(heap_caps_aligned_alloc(16, kBandBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!b) {
      ESP_LOGE(TAG, "bounce buffer alloc failed (%u B internal DMA each)", static_cast<unsigned>(kBandBytes));
      return ESP_ERR_NO_MEM;
    }
  }

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << PCD_PIN_LCD_CS;
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  cs(false);

  spi_bus_config_t bus = {};
  bus.sclk_io_num = PCD_PIN_LCD_SCK;
  bus.data0_io_num = PCD_PIN_LCD_D0;
  bus.data1_io_num = PCD_PIN_LCD_D1;
  bus.data2_io_num = PCD_PIN_LCD_D2;
  bus.data3_io_num = PCD_PIN_LCD_D3;
  bus.data4_io_num = -1;
  bus.data5_io_num = -1;
  bus.data6_io_num = -1;
  bus.data7_io_num = -1;
  bus.max_transfer_sz = kBandBytes + 8;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
    return err;
  }

  spi_device_interface_config_t dev = {};
  dev.command_bits = 8;
  dev.address_bits = 24;
  dev.mode = 0;
  dev.clock_speed_hz = PCD_LCD_PCLK_HZ;
  dev.spics_io_num = -1;  // manual CS: one CS-low period for a whole frame
  dev.flags = SPI_DEVICE_HALFDUPLEX;
  dev.queue_size = 2;
  err = spi_bus_add_device(SPI2_HOST, &dev, &s_dev);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
    return err;
  }

  // RST is not wired: software reset, then the vendor init table.
  ESP_RETURN_ON_ERROR(tx_cmd(kCmdSwReset, nullptr, 0), TAG, "SWRESET");
  vTaskDelay(pdMS_TO_TICKS(120));
  for (const auto &c : kInit) ESP_RETURN_ON_ERROR(tx_cmd(c[0], &c[1], 1), TAG, "init 0x%02X", c[0]);
  const uint8_t zero = 0;
  ESP_RETURN_ON_ERROR(tx_cmd(kCmdSlpOut, &zero, 1), TAG, "SLPOUT");
  vTaskDelay(pdMS_TO_TICKS(120));
  ESP_RETURN_ON_ERROR(tx_cmd(kCmdDispOn, &zero, 1), TAG, "DISPON");
  vTaskDelay(pdMS_TO_TICKS(100));
  // IPS panel: the vendor demo creates the driver with ips=true -> INVON after the table.
  ESP_RETURN_ON_ERROR(tx_cmd(kCmdInvOn, nullptr, 0), TAG, "INVON");
  ESP_LOGI(TAG, "panel ready (%dx%d, bands of %d rows)", PCD_LCD_H_RES, PCD_LCD_V_RES, kBandRows);
  return ESP_OK;
}

esp_err_t set_display_on(bool on) {
  if (!s_dev) return ESP_ERR_INVALID_STATE;
  return tx_cmd(on ? kCmdDispOn : kCmdDispOff, nullptr, 0);
}

esp_err_t sleep_in() {
  if (!s_dev) return ESP_ERR_INVALID_STATE;
  tx_cmd(kCmdDispOff, nullptr, 0);
  const esp_err_t err = tx_cmd(kCmdSlpIn, nullptr, 0);
  vTaskDelay(pdMS_TO_TICKS(120));
  return err;
}

esp_err_t push_rows(const uint16_t *fb, int y0, int y1) {
  if (!s_dev || !fb) return ESP_ERR_INVALID_STATE;
  if (y0 < 0) y0 = 0;
  if (y1 >= PCD_LCD_V_RES) y1 = PCD_LCD_V_RES - 1;
  if (y1 < y0) return ESP_OK;
  const int64_t t0 = esp_timer_get_time();

  ESP_RETURN_ON_ERROR(tx_window_param(kCmdCaset, 0, PCD_LCD_H_RES - 1), TAG, "CASET");
  ESP_RETURN_ON_ERROR(tx_window_param(kCmdRaset, static_cast<uint16_t>(y0), static_cast<uint16_t>(y1)), TAG, "RASET");
  ESP_RETURN_ON_ERROR(tx_cmd(kCmdRamWr, nullptr, 0), TAG, "RAMWR");

  // One CS-low period: the first chunk carries opcode + address, the next ones are raw data.
  const uint16_t *src = fb + static_cast<size_t>(y0) * PCD_LCD_H_RES;
  size_t left = static_cast<size_t>(y1 - y0 + 1) * PCD_LCD_H_RES;
  int inflight = 0, idx = 0;
  bool first = true;
  esp_err_t err = ESP_OK;
  cs(true);
  while (left > 0) {
    if (inflight == 2) {  // results come back in queue order: frees the oldest buffer
      spi_transaction_t *done = nullptr;
      spi_device_get_trans_result(s_dev, &done, portMAX_DELAY);
      inflight--;
    }
    const size_t n = left > kBandPixels ? kBandPixels : left;
    uint16_t *buf = s_bounce[idx];
    memcpy(buf, src, n * sizeof(uint16_t));
    spi_transaction_ext_t &t = s_tx[idx];
    memset(&t, 0, sizeof(t));
    if (first) {
      t.base.flags = SPI_TRANS_MODE_QIO;
      t.base.cmd = kOpColor;
      t.base.addr = kAddrRamWrC;
      first = false;
    } else {
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
      t.command_bits = 0;
      t.address_bits = 0;
      t.dummy_bits = 0;
    }
    t.base.tx_buffer = buf;
    t.base.length = n * 16;
    err = spi_device_queue_trans(s_dev, &t.base, portMAX_DELAY);
    if (err != ESP_OK) break;
    inflight++;
    idx ^= 1;
    src += n;
    left -= n;
  }
  while (inflight > 0) {
    spi_transaction_t *done = nullptr;
    spi_device_get_trans_result(s_dev, &done, portMAX_DELAY);
    inflight--;
  }
  cs(false);
  s_last_push_us = static_cast<uint32_t>(esp_timer_get_time() - t0);
  return err;
}

esp_err_t push_frame(const uint16_t *fb) { return push_rows(fb, 0, PCD_LCD_V_RES - 1); }

uint32_t last_push_us() { return s_last_push_us; }

}  // namespace lcd
