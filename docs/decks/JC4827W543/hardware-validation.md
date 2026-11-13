# Hardware validation — Guition JC4827W543

Legend: ✅ verified on the real board · 🖥️ verified by tool/log/build only · 📄 from vendor sources ·
⏳ pending physical check · ❌ failed

The JC3248W535 bring-up record (AXS15231B, firmware ≤ 0.7.0) is in [its own page](../JC3248W535/hardware-validation.md).

## 1. Board identity

| Item | Result | Evidence |
|---|---|---|
| Module | 📄 ESP32-S3-WROOM-1 **N4R8**: 4 MB flash, 8 MB octal PSRAM | spec PDF; factory binary header (4 MB) |
| Flash chip | 📄 Winbond W25Q32 (id `0xEF 0x4016`, 4 MB) | vendor flash-tool logs |
| Factory partition table | 📄 nvs 20 KB · otadata · app0 3 MB · spiffs 896 KB · coredump 64 KB (= `huge_app.csv`) | factory `JC4827W543C_I.bin` |
| Touch variant | ⏳ C = capacitive (GT911); R = resistive (XPT2046, not supported); N = no touch | label on the back |
| USB enumeration / COM port | ⏳ native USB-Serial/JTAG (`303A:1001`) expected | — |

## 2. Pin map

| Function | GPIO | Source |
|---|---|---|
| LCD CS / SCK | 45 / 47 | 📄 demo `Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39)`, IO sheet |
| LCD D0..D3 | 21 / 48 / 40 / 39 | 📄 same |
| LCD TE | 0 (shared with BOOT) | 📄 IO sheet; tearing output is never enabled |
| Backlight (PWM) | 1 | 📄 demo `GFX_BL 1`, IO sheet |
| LCD RST | not wired (software reset) | 📄 demo `GFX_NOT_DEFINED` |
| Touch SDA / SCL | 8 / 4 | 📄 demo `touch.h` (note: swapped vs. the JC3248W535) |
| Touch INT / RST | 3 / 38 | 📄 demo `touch.h`, IO sheet |
| Sleep button (header) | 17 / 18 | IO sheet (U1TXD / U1RXD, free) |
| TF card | CS 10, MISO 11, CLK 12, MOSI 13 | 📄 IO sheet (unused) |
| Speaker (I2S) | LRCLK 2, DIN 41, BCLK 42 | 📄 IO sheet (unused) |
| Battery sense | none in the pin map | 📄 IO sheet |

## 3. Firmware build

| Check | Result |
|---|---|
| `pio run -e deck` | 🖥️ builds; RAM 20.7 %, flash 2.37 MB / 3 MB (firmware 0.10.0) |
| `pio run -e hwtest` | 🖥️ builds |
| `pio test -e test` (compile) | 🖥️ builds; protocol tests also run natively (10/10 pass, crypto fixtures included) |
| UI layouts | 🖥️ every page rendered at 480×272 by `firmware/sim` (ES and EN), screensaver and wallpaper picker included |

## 4. To check on the board (`env:hwtest` first)

| Check | How | Fix if wrong |
|---|---|---|
| Panel lights up, colours right (RED is red, WHITE is white) | colour bars | inverted colours: drop the INVON in `lcd_nv3041a.cpp`; RGB/BGR: MADCTL bit 3 |
| Orientation (title at the top, not mirrored) | hwtest screen | MADCTL `0xC0` in the init table (0x00 / 0x40 / 0x80 variants) |
| CASET/RASET framing | stable image, no shifted / torn lines | `-DPCD_LCD_SPLIT_PARAMS=0` |
| Frame time | log `push … us` ≈ 16 ms at 32 MHz | — |
| GT911 found | log `GT911 '911' at 0x5D, configured 480x272` | address 0x14 is probed too |
| Touch mapping | the four red corners turn green where touched | `PCD_TOUCH_SWAP_XY` / `MIRROR_X` / `MIRROR_Y` |
| Partial row pushes (optional) | `-DPCD_LCD_PARTIAL_ROWS=1`: no artefacts while the clock / spinner update | keep 0 (default) |
| Sleep button | serial `status`: idle levels, taps; deep sleep log shows the ext1/ext0 choice | momentary mode on the module |
| Wake from deep sleep | button tap, touch, BOOT | log `woke from deep sleep (button\|touch\|boot)` |
| BOOT / TE on GPIO0 | BOOT turns a dark screen on; no spurious wake-ups with the screen off | — |
| Screensaver wallpapers | `saver bg 0..7` + `saver`: every image decodes without artefacts and with the right colours (JPEGDEC's S3 SIMD path, big-endian RGB565); the picker shows 8 thumbnails | JPEGDEC options (`NO_SIMD`); re-run `tools/gen_wallpapers.py` |
| Screensaver crossfade / fade-in | Rotate every 1 min: the change is smooth, no tearing that matters (whole-frame pushes) | `kCrossfadeMs` |
| Screensaver exit | a touch closes it without pressing what is underneath; BOOT closes it; a thermal alert closes it; the IO17/18 button closes it and runs its action | — |
| Screensaver cards | a tap on the PC / music / crypto card or the weather line opens Monitor / Player / that coin's chart / Today; the card lights up while pressed | — |
| Screensaver vs deep sleep | screensaver on + auto sleep set: the deck never deep-sleeps by itself | — |
| Wi-Fi page scanning | opening the page lists networks within a few seconds; it re-scans every 15 s while shown, the telemetry link stays up during scans | `kRescanMs` |
