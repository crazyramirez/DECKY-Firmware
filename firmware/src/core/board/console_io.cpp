#include "board/console_io.h"

#include <Arduino.h>
#include <stdarg.h>

#include "deck_config.h"
#if PCD_USB_CONSOLE
#include "board/usb_console.h"
#endif

namespace con {

#if PCD_USB_CONSOLE

void begin() {
  // Logs: ESP-IDF console (USE_ESP_IDF_LOG); the prebuilt sdkconfig defaults to ERROR.
  esp_log_level_set("*", ESP_LOG_INFO);
  usbcon::begin();
}
void println(const char *s) { usbcon::println(s); }
void printf(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  usbcon::print(buf);
}
size_t write(const void *data, size_t len, uint32_t timeout_ms) { return usbcon::write(data, len, timeout_ms); }
void flush() { usbcon::flush(); }
int read(uint8_t *buf, size_t max) { return usbcon::read(buf, max); }

#else

void begin() { Serial.begin(115200); }
void println(const char *s) { Serial.println(s); }
void printf(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
}
size_t write(const void *data, size_t len, uint32_t timeout_ms) {
#if ARDUINO_USB_CDC_ON_BOOT  // Serial is the USB-Serial/JTAG (HWCDC): it drops what the host doesn't read
  Serial.setTxTimeoutMs(timeout_ms);  // block (instead of dropping) while the host drains the FIFO
  const size_t n = Serial.write(static_cast<const uint8_t *>(data), len);
  Serial.setTxTimeoutMs(100);
  return n;
#else  // Serial is UART0 behind a USB-UART bridge: writes block until the bytes are on their way
  (void)timeout_ms;
  return Serial.write(static_cast<const uint8_t *>(data), len);
#endif
}
void flush() { Serial.flush(); }
int read(uint8_t *buf, size_t max) {
  size_t n = 0;
  while (n < max && Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) break;
    buf[n++] = static_cast<uint8_t>(ch);
  }
  return static_cast<int>(n);
}

#endif

}  // namespace con
