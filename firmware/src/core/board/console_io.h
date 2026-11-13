#pragma once
#include <stddef.h>
#include <stdint.h>

// The deck's USB console (logs + the command console of network/serial_console). Decks with
// PCD_USB_CONSOLE (the ESP32-P4) go through board/usb_console (Arduino's HWCDC Serial is off there);
// the others through Arduino's Serial: the S3's USB-Serial/JTAG, or UART0 behind a USB-UART bridge
// (CH340) on the boards whose USB pins drive the panel (ARDUINO_USB_CDC_ON_BOOT=0).
namespace con {

void begin();  // early in setup(): logs at INFO, console input
void println(const char *s);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Blocking write of a bulk answer (screenshots): bytes written, fewer when no host is reading.
size_t write(const void *data, size_t len, uint32_t timeout_ms);
void flush();
int read(uint8_t *buf, size_t max);  // non-blocking; bytes read

}  // namespace con
