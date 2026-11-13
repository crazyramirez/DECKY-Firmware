# Hardware validation — Guition JC1060P470C

Legend: ✅ verified on the real board · 🖥️ verified by tool/log/build only · 📄 from vendor sources or
the JC1060P470C CryptoTracker (same board, same platform) · ⏳ pending physical check · ❌ failed

The JC4827W543 and JC3248W535 bring-up records are in their repositories
([JC4827W543](https://github.com/crazyramirez/PC-Control-Deck-ESP32-JC4827W543/blob/main/docs/HARDWARE_VALIDATION.md),
[JC3248W535](https://github.com/crazyramirez/PC-Control-Deck-ESP32-JC3248W535)).

## 1. Board identity

| Item | Result | Evidence |
|---|---|---|
| SoC | 📄 ESP32-P4 (silicon v1.x on every board shipped so far), 32 MB PSRAM, 16 MB flash | CryptoTracker `platformio.ini` (`esp32-p4-evboard`, 16 MB) |
| Wi-Fi | 📄 ESP32-C6 co-processor over ESP-Hosted (SDIO), Arduino `WiFi.h` unchanged | CryptoTracker network task |
| Display | 📄 7" 1024×600 IPS, JD9165 MIPI-DSI, 2 lanes | vendor demo `jd9165_lcd.cpp`, CryptoTracker |
| Touch | 📄 GT911 capacitive | vendor demo `gt911_touch.cpp` |
| USB / COM port | 📄 USB-C = ESP32-P4 USB-Serial/JTAG (`303A:1001`) | CryptoTracker logs on COM3 |

## 2. Pin map

| Function | GPIO | Source |
|---|---|---|
| LCD reset | 5 | 📄 vendor `pins_config.h` (`LCD_RST 5`) |
| Backlight (PWM) | 23 | 📄 CryptoTracker `PIN_LCD_BL 23` (vendor demo: `LCD_LED -1`) |
| MIPI D-PHY supply | internal LDO channel 3, 2.5 V | 📄 CryptoTracker / Espressif P4 examples |
| Touch SDA / SCL | 7 / 8 | 📄 vendor `pins_config.h` |
| Touch INT / RST | not wired | 📄 vendor `TP_INT -1`, `TP_RST -1` |
| BOOT button | 35 (active low) | 📄 CryptoTracker `PIN_BOOT_BTN 35` |
| C6 SDIO (ESP-Hosted) | framework defaults of `esp32-p4-evboard` | 📄 CryptoTracker uses the defaults |
| Battery sense | none | — |

## 3. Firmware build

| Check | Result |
|---|---|
| `pio run -e deck` | 🖥️ builds; RAM 10.5 %, flash 2.89 MB / 7 MB |
| `pio run -e hwtest` | 🖥️ builds |
| merged image (`esptool 4.8.1 --chip esp32p4 merge_bin`, bootloader at `0x2000`) | 🖥️ created, 3.0 MB |
| UI layouts | 🖥️ every page rendered at 1024×600 by `firmware/sim` (ES and EN) |

## 4. To check on the board (`env:hwtest` first)

| Check | How | Fix if wrong |
|---|---|---|
| Panel lights up, colours right (RED is red, WHITE is white) | hwtest colour bars | RGB/BGR: `rgb_ele_order` in `lcd_jd9165.cpp`; byte order: `LV_COLOR_16_SWAP` (must stay 0) |
| Stable image, no tearing artefacts that matter | hwtest, scroll Settings | DPI clock `PCD_DPI_CLOCK_MHZ`, band height `PCD_LVGL_BUF_LINES` |
| Band copy time | `status`: `band copy … us` (expected well under 1 ms for 40 lines) | — |
| GT911 found | log `GT911 '911' at 0x5D` (or 0x14), configured 1024x600 | address depends on INT at power-on; both are probed |
| Touch mapping | the four red corners turn green where touched | `PCD_TOUCH_SWAP_XY` / `MIRROR_X` / `MIRROR_Y` |
| Backlight | hwtest slider 5..100 %, no audible whine (20 kHz PWM) | — |
| BOOT button | hwtest info line `BOOT down/up`; in the deck it turns the screen off/on | — |
| Wi-Fi through the C6 | ✅ scan lists networks (0.11.1); deck connects and pairs ⏳ | ESP-Hosted slave firmware on the C6 (2.3.2 on the test board) |
| USB console | logs visible on the COM port; `help`, `status`, `shot` answer | — |
| Silicon revision | esptool `Chip is ESP32-P4 (revision v1.x)` → `deck`; v3.x → `deck_r3` | — |
| Screensaver wallpapers | `saver bg 0..7` + `saver`: every image decodes without artefacts; the picker shows 8 thumbnails | JPEGDEC options; re-run `tools/gen_wallpapers.py` |
| Screensaver crossfade / fade-in | Rotate every 1 min: the change is smooth, no tearing that matters | `kCrossfadeMs`, band height |
| Screensaver exit | a touch closes it without pressing what is underneath; BOOT closes it; a thermal alert closes it | — |
