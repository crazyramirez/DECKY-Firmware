#include "usb_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace usbcon {
namespace {
bool s_ready = false;
}  // namespace

bool begin() {
  if (s_ready) return true;
  usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  cfg.rx_buffer_size = 1024;
  cfg.tx_buffer_size = 4096;
  s_ready = usb_serial_jtag_driver_install(&cfg) == ESP_OK;
  if (!s_ready) ESP_LOGE("usbcon", "USB-Serial/JTAG driver install failed");
  return s_ready;
}

int read(uint8_t *buf, size_t max) {
  if (!s_ready || !max) return 0;
  const int n = usb_serial_jtag_read_bytes(buf, max, 0);
  return n > 0 ? n : 0;
}

bool connected() { return usb_serial_jtag_is_connected(); }

size_t write(const void *data, size_t len, uint32_t timeout_ms) {
  if (!s_ready || !len || !connected()) return 0;
  const int n = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(timeout_ms));
  return n > 0 ? static_cast<size_t>(n) : 0;
}

void print(const char *s) { write(s, strlen(s)); }

void println(const char *s) {
  write(s, strlen(s));
  write("\n", 1);
}

void printf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) write(buf, n < static_cast<int>(sizeof(buf)) ? static_cast<size_t>(n) : sizeof(buf) - 1);
}

void flush(uint32_t timeout_ms) {
  if (s_ready && connected()) usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(timeout_ms));
}

}  // namespace usbcon
