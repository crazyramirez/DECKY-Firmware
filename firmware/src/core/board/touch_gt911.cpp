#include "deck_config.h"
#if PCD_TOUCH_GT911  // the decks with a GT911 (deck_config.h)

#include "touch_gt911.h"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "board_pins.h"

namespace touch {
namespace {

const char *TAG = "touch";
constexpr uint16_t kRegConfigRes = 0x8048;
constexpr uint16_t kRegProductId = 0x8140;
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegPoint1 = 0x814F;
constexpr uint32_t kStaleReleaseMs = 150;  // pressed but no fresh report for this long -> released

uint8_t s_addr = 0;
uint8_t s_raw[8] = {};
Stats s_stats;
uint16_t s_cfg_x = 0, s_cfg_y = 0;
Point s_last;
uint32_t s_last_fresh_ms = 0;

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission(true) == 0;
}

bool read_reg(uint16_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(s_addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(s_addr, n) != n) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < n; i++) buf[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

bool write_reg8(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(s_addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(v);
  return Wire.endTransmission(true) == 0;
}

// GT911 power-on reset selecting I2C address 0x5D (INT low while RST rises). Boards without the
// RST/INT lines wired (JC1060P470C) keep the controller's own power-on reset and address.
void hw_reset() {
#if PCD_PIN_TOUCH_RST < 0 || PCD_PIN_TOUCH_INT < 0
  delay(10);
#else
  pinMode(PCD_PIN_TOUCH_INT, OUTPUT);
  digitalWrite(PCD_PIN_TOUCH_INT, LOW);
  pinMode(PCD_PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PCD_PIN_TOUCH_RST, LOW);
  delay(12);
  digitalWrite(PCD_PIN_TOUCH_RST, HIGH);
  delay(60);  // INT must stay low >= 50 ms after reset, then it becomes the controller's output
  pinMode(PCD_PIN_TOUCH_INT, INPUT);
  delay(50);
#endif
}

}  // namespace

bool init() {
  if (s_addr) return true;  // already up (a deck may check a touch wake before the display starts)
  hw_reset();
  if (!Wire.begin(PCD_PIN_TOUCH_SDA, PCD_PIN_TOUCH_SCL, PCD_TOUCH_I2C_HZ)) {
    ESP_LOGE(TAG, "Wire.begin(SDA=%d, SCL=%d) failed", PCD_PIN_TOUCH_SDA, PCD_PIN_TOUCH_SCL);
    return false;
  }
  Wire.setTimeOut(20);
  for (uint8_t a : {static_cast<uint8_t>(PCD_TOUCH_I2C_ADDR), static_cast<uint8_t>(0x5D), static_cast<uint8_t>(0x14)}) {
    if (probe(a)) {
      s_addr = a;
      break;
    }
  }
  if (!s_addr) {
    ESP_LOGE(TAG, "GT911 not found at 0x5D/0x14 (SDA=%d SCL=%d)", PCD_PIN_TOUCH_SDA, PCD_PIN_TOUCH_SCL);
    return false;
  }
  char id[5] = "";
  uint8_t res[4] = {};
  read_reg(kRegProductId, reinterpret_cast<uint8_t *>(id), 4);
  id[4] = '\0';
  if (read_reg(kRegConfigRes, res, 4)) {
    s_cfg_x = static_cast<uint16_t>(res[0] | (res[1] << 8));
    s_cfg_y = static_cast<uint16_t>(res[2] | (res[3] << 8));
  }
  write_reg8(kRegStatus, 0);
  ESP_LOGI(TAG, "GT911 '%s' at 0x%02X, configured %ux%u (panel %dx%d)", id, s_addr, s_cfg_x, s_cfg_y, PCD_LCD_H_RES,
           PCD_LCD_V_RES);
  return true;
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
  out = s_last;
  if (!s_addr) {
    out.pressed = false;
    return false;
  }
  s_stats.reads++;
  uint8_t status = 0;
  if (!read_reg(kRegStatus, &status, 1)) {
    s_stats.i2c_errors++;
    return false;
  }
  const uint32_t now = millis();
  if (!(status & 0x80)) {
    // No new report: keep the previous state, but never hold a press forever.
    if (s_last.pressed && now - s_last_fresh_ms > kStaleReleaseMs) s_last.pressed = false;
    out = s_last;
    return true;
  }
  s_last_fresh_ms = now;
  const uint8_t points = status & 0x0F;
  s_raw[0] = status;
  bool pressed = false;
  if (points >= 1 && points <= 5) {
    uint8_t p[7] = {};
    if (read_reg(kRegPoint1, p, sizeof(p))) {
      memcpy(s_raw + 1, p, sizeof(p));
      uint32_t x = static_cast<uint32_t>(p[1] | (p[2] << 8));
      uint32_t y = static_cast<uint32_t>(p[3] | (p[4] << 8));
#if PCD_TOUCH_SWAP_XY
      const uint32_t t = x;
      x = y;
      y = t;
      const uint16_t cx = s_cfg_y, cy = s_cfg_x;
#else
      const uint16_t cx = s_cfg_x, cy = s_cfg_y;
#endif
      // Controller configured for another resolution: scale into panel coordinates.
      if (cx > 0 && cx != PCD_LCD_H_RES) x = x * PCD_LCD_H_RES / cx;
      if (cy > 0 && cy != PCD_LCD_V_RES) y = y * PCD_LCD_V_RES / cy;
      if (x < PCD_LCD_H_RES && y < PCD_LCD_V_RES) {
#if PCD_TOUCH_MIRROR_X
        x = PCD_LCD_H_RES - 1 - x;
#endif
#if PCD_TOUCH_MIRROR_Y
        y = PCD_LCD_V_RES - 1 - y;
#endif
        s_last.x = static_cast<uint16_t>(x);
        s_last.y = static_cast<uint16_t>(y);
        pressed = true;
      } else {
        s_stats.rejected++;
      }
    } else {
      s_stats.i2c_errors++;
    }
  } else if (points > 5) {
    s_stats.rejected++;
  }
  s_last.pressed = pressed;
  write_reg8(kRegStatus, 0);  // hand the buffer back to the controller
  out = s_last;
  return true;
}

const uint8_t *last_raw() { return s_raw; }
const Stats &stats() { return s_stats; }
uint8_t address() { return s_addr; }
void config_resolution(uint16_t &x, uint16_t &y) {
  x = s_cfg_x;
  y = s_cfg_y;
}

}  // namespace touch

#endif  // PCD_TOUCH_GT911
