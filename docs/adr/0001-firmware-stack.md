# ADR 0001 — Firmware stack: Arduino-ESP32 3.3.5 (pioarduino) + own AXS15231B driver + LVGL 8.4.0

- **Status:** accepted (pending physical sign-off in `docs/HARDWARE_VALIDATION.md`)
- **Date:** 2026-11-11

## Context

The spec asks for PlatformIO + C++ + LVGL, Arduino if a working reference exists, and a pinned
LVGL version compatible with EEZ Studio. Two references were evaluated:

| Reference | Framework | LVGL | Notes |
|---|---|---|---|
| GthiN89/JC3248W535EN @825f644 (2024-11) | Arduino, **unpinned** `arduino-esp32.git` master + IDF 5.1 libs | 8.3 branch | Espressif `esp_lcd_axs15231b` with the *inverted* `disp_on_off`; landscape via software rotation |
| GlomarGadaffi/jc3248-display-driver @773c5b5 (2026-10) | ESP-IDF | none (raw framebuffer) | Fixed `disp_on_off`, different "verified JC3248W535EN" init table, documents bring-up traps |

Neither can be used as-is: GthiN89 depends on a moving git branch (non-reproducible), Glomar is
IDF-only and has no LVGL integration.

## Decision

1. **Platform:** `pioarduino/platform-espressif32` **55.03.35** → Arduino-ESP32 **3.3.5** on ESP-IDF **5.5.1**.
   Pinned by URL in `platformio.ini`. Arduino gives us mature Wi-Fi, `Preferences` (NVS),
   `WebSockets`/`ArduinoJson` libraries for Hito 3, while still exposing `esp_lcd` for the panel.
2. **Panel driver:** own small module `src/board/lcd_axs15231b.*` built directly on
   `esp_lcd_panel_io` (QSPI, 32-bit command framing `0x02/0x32 << 24 | cmd << 8`).
   Both init tables are embedded and selectable with `-DPCD_LCD_INIT_TABLE=1|2`; table 1
   (tsebelev/Glomar) is the default. DISPON is always sent explicitly after init.
3. **Flush strategy:** LVGL **direct mode** with one full-screen PSRAM buffer (300 KiB).
   LVGL redraws only dirty areas; on the last area the whole frame is streamed top-to-bottom
   (`RAMWR` + `RAMWRC`) through two 20 KiB internal-RAM DMA bounce buffers. Reason: over QSPI
   the panel only accepts full top-to-bottom frames (RASET not honoured by the reference
   drivers), and DMA directly from PSRAM risks SPI FIFO underflow.
   Theoretical transfer: 307 200 B at 40 MHz × 4 lines ≈ 15 ms/frame.
4. **Touch:** polled over Arduino `Wire` (I²C 0x3B, SDA 4 / SCL 8, 400 kHz) with
   range/point-count validation; transform flags `PCD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y`.
5. **LVGL 8.4.0** (registry `lvgl/lvgl@8.4.0`). Same v8 API as both references and supported by
   EEZ Studio's LVGL 8.x target. LVGL 9 migration is out of scope for V1.
6. **Native portrait** (MADCTL 0x00): the panel is physically 320×480, so no software rotation.
7. Console on the native USB-Serial/JTAG (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`), COM port
   enumerates as VID 303A / PID 1001.

## Consequences

- Every refresh costs a full-frame transfer (~15–20 ms), acceptable for a 1 Hz dashboard;
  FPS is measured by the hwtest app and logged.
- If physical tests show RASET works over QSPI, a partial-update path can replace full pushes
  later (measure first).
- EEZ Studio projects must target **LVGL 8.x**, and generated code lives in
  `firmware/src/ui/eez_generated/` only.
