#include "touch_axs15231b.h"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "board_pins.h"

namespace touch {
namespace {

const char *TAG = "touch";
constexpr uint8_t kReadCmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
constexpr uint8_t kReplyLen = 8;
constexpr uint8_t kEventLiftUp = 1;

uint8_t s_raw[kReplyLen] = {};
Stats s_stats;
bool s_ok = false;

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission(true) == 0;
}

}  // namespace

bool init() {
  if (s_ok) return true;  // already up (power checks a touch wake before the display starts)
  if (!Wire.begin(PCD_PIN_TOUCH_SDA, PCD_PIN_TOUCH_SCL, PCD_TOUCH_I2C_HZ)) {
    ESP_LOGE(TAG, "Wire.begin(SDA=%d, SCL=%d) failed", PCD_PIN_TOUCH_SDA, PCD_PIN_TOUCH_SCL);
    return false;
  }
  Wire.setTimeOut(20);
  s_ok = probe(PCD_TOUCH_I2C_ADDR);
  ESP_LOGI(TAG, "controller 0x%02X %s (SDA=%d SCL=%d %d kHz)", PCD_TOUCH_I2C_ADDR, s_ok ? "ACK" : "NO ACK",
           PCD_PIN_TOUCH_SDA, PCD_PIN_TOUCH_SCL, PCD_TOUCH_I2C_HZ / 1000);
  return s_ok;
}

int scan_bus(uint8_t *found, int max_found) {
  int n = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (probe(a)) {
      if (n < max_found) found[n] = a;
      n++;
    }
  }
  return n;
}

bool read(Point &out) {
  out.pressed = false;
  s_stats.reads++;

  Wire.beginTransmission(PCD_TOUCH_I2C_ADDR);
  Wire.write(kReadCmd, sizeof(kReadCmd));
  if (Wire.endTransmission(true) != 0) {
    s_stats.i2c_errors++;
    return false;
  }
  if (Wire.requestFrom(static_cast<uint8_t>(PCD_TOUCH_I2C_ADDR), kReplyLen) != kReplyLen) {
    s_stats.i2c_errors++;
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < kReplyLen; i++) s_raw[i] = static_cast<uint8_t>(Wire.read());

  const uint8_t points = s_raw[1];
  const uint8_t event = s_raw[2] >> 6;
  if (points == 0 || event == kEventLiftUp) return true;
  if (points > 1) {  // single-touch firmware; anything else is a garbage frame
    s_stats.rejected++;
    return true;
  }

  uint16_t x = static_cast<uint16_t>(((s_raw[2] & 0x0F) << 8) | s_raw[3]);
  uint16_t y = static_cast<uint16_t>(((s_raw[4] & 0x0F) << 8) | s_raw[5]);
#if PCD_TOUCH_SWAP_XY
  const uint16_t t = x;
  x = y;
  y = t;
#endif
  if (x >= PCD_LCD_H_RES || y >= PCD_LCD_V_RES) {
    s_stats.rejected++;
    return true;
  }
#if PCD_TOUCH_MIRROR_X
  x = PCD_LCD_H_RES - 1 - x;
#endif
#if PCD_TOUCH_MIRROR_Y
  y = PCD_LCD_V_RES - 1 - y;
#endif
  out.x = x;
  out.y = y;
  out.pressed = true;
  return true;
}

const uint8_t *last_raw() { return s_raw; }
const Stats &stats() { return s_stats; }

}  // namespace touch
