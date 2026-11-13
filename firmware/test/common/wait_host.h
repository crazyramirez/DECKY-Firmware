#pragma once
// USB-Serial/JTAG (HWCDC) drops output while no host is reading. After an upload/reset the PC
// re-enumerates the port, so tests wait until the host is actually connected before printing.
#include <Arduino.h>

static inline void wait_for_test_host() {
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!HWCDC::isConnected() && millis() - t0 < 20000) delay(50);
  delay(1500);  // let the test runner finish opening the port
}
