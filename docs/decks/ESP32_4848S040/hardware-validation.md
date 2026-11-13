# Hardware validation — Guition ESP32-4848S040

Legend: ✅ verified on the real board · 🖥️ verified by tool/log/build only · 📄 from public sources ·
⏳ pending physical check · ❌ failed

Board under test: MAC `24:58:7C:D6:E7:0C`, ESP32-S3 (QFN56) rev v0.2, 2026-11-13. Factory flash
backed up first (`backups/`, not in git).

## 1. Board identity

| Item | Result | Evidence |
|---|---|---|
| SoC | ✅ ESP32-S3 rev v0.2, embedded 8 MB PSRAM (AP 3.3 V, octal) | esptool `flash_id` |
| Flash | ✅ 16 MB, quad, 3.3 V (manufacturer 0x85, device 0x2018) | esptool `flash_id` |
| USB | ✅ CH340 (VID 1A86, PID 7523) on UART0; auto-reset into the bootloader works | esptool, PlatformIO upload |
| CH340 speed | ✅ writes at 460800 verified; ❌ `read-flash` corrupts at 460800 and 921600, works at 230400 | esptool |

## 2. Display

| Item | Result | Evidence |
|---|---|---|
| Pins (RGB bus, 3-wire SPI, backlight GPIO38) | 📄 Arduino_GFX `ESP32_4848S040_86BOX_GUITION` and the Sunton board file agree on every pin | `board_pins.h` |
| Init sequence | 📄 Arduino_GFX `st7701_type9_init_operations` ("by manufacture": `0xCD 0x00`, no inversion) | `lcd_st7701.cpp` |
| Timings | 📄 HSYNC 8/50/10, VSYNC 8/20/10, syncs idle high, PCLK 12 MHz (the board file says 12.5) | both sources |
| Panel up, frame buffer in PSRAM | ✅ `ST7701S 480x480 ready`, 460 800 B frame buffer | boot log |
| Colours, orientation, no drift | ✅ confirmed by eye: the setup wizard's colours, upright | setup wizard |
| Drawing straight into the scanned frame buffer | ❌ flashes on every key press (LVGL draws an area in layers): LVGL now draws into its own frame, copied at VSYNC: ✅ no flashes | user report |
| Backlight PWM | ❌ at 5 kHz the screen goes black below ~50 %; 150 Hz (ESPHome's setting for this board): ✅ dims smoothly | user report |
| Copy to the panel by DMA (GDMA) | ❌ flashes: it competes with the panel's DMA for PSRAM; the copy stays the CPU's | user report |
| Software restart | ❌ hung in the ROM until the RTC watchdog (panel DMA still reading PSRAM); ✅ 5/5 clean once the panel is stopped first | console `reboot` |

## 3. Touch

| Item | Result | Evidence |
|---|---|---|
| GT911 on SDA 19 / SCL 45 | ✅ ACK at `0x5D`, product id `911`, configured 480×480 | boot log, I²C scan |
| Mapping (no swap / mirror) | ✅ the Wi-Fi password typed on the on-screen keyboard, network joined | setup wizard |

## 4. Wi-Fi

| Item | Result | Evidence |
|---|---|---|
| Scan | ✅ 5 networks, best −52 dBm, 2.6 s | hwtest log |

## 5. Firmware

| Item | Result | Evidence |
|---|---|---|
| `id` | ✅ `pcdeck board=ESP32_4848S040 fw=…` | USB console |
| Setup wizard on a fresh NVS | ✅ shown at boot | `shot` over USB |
