#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * USB console on the ESP32-P4's USB-Serial/JTAG port (the board's USB-C, a COM port on the PC).
 *
 * Arduino's HWCDC Serial is not used on the P4 (ARDUINO_USB_CDC_ON_BOOT=0): its TX interrupt faults
 * on the USB-Serial/JTAG FIFO under load. Logs go through the ESP-IDF console (UART0 + this port as
 * secondary output, ESP_LOGx with USE_ESP_IDF_LOG); this module adds input (the IDF driver, RX only
 * until something is written) and blocking writes for command answers and screenshots.
 */
namespace usbcon {

bool begin();                                 // installs the USB-Serial/JTAG driver
int read(uint8_t *buf, size_t max);           // non-blocking; bytes read
size_t write(const void *data, size_t len, uint32_t timeout_ms = 100);  // dropped when no host
void print(const char *s);
void println(const char *s);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void flush(uint32_t timeout_ms = 500);
bool connected();                             // a USB host is reading the port

}  // namespace usbcon
