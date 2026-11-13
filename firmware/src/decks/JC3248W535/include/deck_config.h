#pragma once
// Guition JC3248W535: 3.5" 320x480 AXS15231B (portrait), ESP32-S3 N16R8.
// What the shared code (src/core) needs to know about this deck. Pins: board_pins.h; LVGL: lv_conf.h.

#define PCD_DECK_KEY "JC3248W535"  // model id: console `id`, boot log; the PC app's USB updater matches it

// Wi-Fi is an ESP32-C6 behind ESP-Hosted: never WIFI_OFF (the SDIO link would not come back), the
// identity uses the P4's base MAC, scans are retried while the C6 starts.
#define PCD_WIFI_ESP_HOSTED 0
// The deck can wake from deep sleep (touch INT / BOOT on RTC GPIOs): auto sleep and the console
// `sleep` command use it. Without it, auto sleep only turns the screen off.
#define PCD_HAS_DEEP_SLEEP 1
// Sleep button on the IO17/IO18 header (e.g. a TTP223 touch-button module), action in Settings.
#define PCD_HAS_SLEEP_BUTTON 0
// Console on the chip's USB-Serial/JTAG through board/usb_console (Arduino's Serial is off).
#define PCD_USB_CONSOLE 0
// GT911 capacitive touch (src/core/board/touch_gt911.*).
#define PCD_TOUCH_GT911 0

#define PCD_BACKLIGHT_HZ 5000  // LEDC PWM frequency of the backlight (10-bit)
#define PCD_COIN_LOGO_S 24   // coin logos in src/decks/<DECK>/crypto/coin_catalog.c (px)
#define PCD_COIN_LOGO_M 32
