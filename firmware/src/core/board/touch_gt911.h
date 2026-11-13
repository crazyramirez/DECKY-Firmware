#pragma once
#include <stdint.h>

/*
 * Goodix GT911 capacitive touch, polled over I2C (Arduino Wire).
 * Reset sequence selects address 0x5D (INT held low while RST rises) when RST/INT are wired; 0x14 is
 * probed as a fallback.
 * Registers: 0x8140 product id, 0x8048..0x804B configured X/Y resolution, 0x814E status
 * (bit7 = new data, bits0-3 = points), 0x814F.. first point (id, xL, xH, yL, yH, sizeL, sizeH, -).
 * Coordinates are scaled when the controller is configured for another resolution than the panel.
 */
namespace touch {

struct Point {
  uint16_t x = 0;
  uint16_t y = 0;
  bool pressed = false;
};

struct Stats {
  uint32_t reads = 0;
  uint32_t i2c_errors = 0;
  uint32_t rejected = 0;  // frames with out-of-range data
};

bool init();                       // reset + Wire on SDA/SCL from board_pins.h; true if the GT911 ACKs
int scan_bus(uint8_t *found, int max_found);  // diagnostic I2C scan, returns count
bool read(Point &out);             // false on I2C error; out.pressed=false when no touch
const uint8_t *last_raw();         // last 8 raw bytes (status + first point), diagnostics
const Stats &stats();
uint8_t address();                 // 0 when not found
void config_resolution(uint16_t &x, uint16_t &y);  // as read from the controller (0 = unknown)

}  // namespace touch
