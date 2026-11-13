#pragma once
#include <stdint.h>

/*
 * AXS15231B touch core, polled over I2C (Arduino Wire).
 * Protocol (from the Espressif/Glomar drivers): write the 11-byte read command
 * B5 AB A5 5A 00 00 00 08 00 00 00, then read 8 bytes:
 *   [0] gesture  [1] points  [2] event<<6 | x[11:8]  [3] x[7:0]  [4] y[11:8]  [5] y[7:0]
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

bool init();                       // Wire on SDA/SCL from board_pins.h; returns true if 0x3B ACKs
int scan_bus(uint8_t *found, int max_found);  // diagnostic I2C scan, returns count
bool read(Point &out);             // false on I2C error; out.pressed=false when no touch
const uint8_t *last_raw();         // last 8 raw bytes, diagnostics
const Stats &stats();

}  // namespace touch
