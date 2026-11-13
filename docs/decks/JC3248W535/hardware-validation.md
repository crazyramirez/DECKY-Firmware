# Hardware validation — Guition JC3248W535 (Hito 0)

Legend: ✅ verified on the real board · 🖥️ verified by tool/log only (no human eyes) · ⏳ pending physical check · ❌ failed

## 1. Board identity

| Item | Result | Evidence |
|---|---|---|
| USB enumeration | 🖥️ COM6, VID `303A` PID `1001` (native USB-Serial/JTAG) | Windows PnP, 2026-11-11 |
| SoC | 🖥️ ESP32-S3 (QFN56) **rev v0.2**, 40 MHz crystal, Wi-Fi + BLE | `esptool flash_id` |
| PSRAM | 🖥️ **8 MB embedded** (`AP_3v3`) → ESP32-S3R8, octal PSRAM | `esptool flash_id` |
| Flash | 🖥️ **16 MB**, quad, 3.3 V (manufacturer `0x85`, device `0x2018`) | `esptool flash_id` |
| MAC | 🖥️ `8c:bf:ea:0d:b0:d8` | `esptool` |
| Silkscreen / exact PCB revision | ⏳ read "JC3248W535" vs "…EN" and version on the back of the PCB | photo |

## 2. Pin map (both references agree)

| Function | GPIO | GthiN89 | Glomar |
|---|---|---|---|
| LCD CS / SCK | 45 / 47 | ✔ | ✔ |
| LCD D0..D3 | 21 / 48 / 40 / 39 | ✔ | ✔ |
| LCD TE | 38 | ✔ | ✔ |
| Backlight (PWM) | 1 | ✔ | ✔ |
| LCD DC / RST | NC | lists DC=8 (unused in QSPI) | NC |
| Touch SDA / SCL | 4 / 8 | ✔ | ✔ |
| Touch INT | 3 | -1 | 3 (polled) |

**Touch controller discrepancy:** the Glomar README says "FT5336", its own pinout doc and
driver say AXS15231B touch core at I²C `0x3B`. The hwtest firmware performs a full I²C scan at
boot and probes `0x3B`; the scan result below is the authority.

| Check | Result |
|---|---|
| I²C scan on SDA 4 / SCL 8 | 🖥️ **1 device: `0x3B`** (no `0x38`, so not FT5336) |
| `0x3B` ACK | 🖥️ ACK; 0 I²C errors over hundreds of reads |
| Touch reports | 🖥️ presses logged at x≈107, y≈396–417 (raw `00 01 80 6A 11 A1 …`) |
| Idle frames | 🖥️ controller returns invalid data when not touched (point count >1 or out of range); filtered and counted as `rejected` |

## 3. Software versions (pinned)

See [ADR 0001](adr/0001-firmware-stack.md).

| Component | Version |
|---|---|
| PlatformIO Core | 6.1.18 |
| Platform | pioarduino `platform-espressif32` 55.03.35 |
| Arduino-ESP32 / ESP-IDF | 3.3.5 / 5.5.1 |
| LVGL | 8.4.0 |
| LCD init table | 1 (tsebelev/Glomar) by default, 2 (Espressif) selectable |
| QSPI clock | 40 MHz |

## 4. Factory firmware

No backup was taken (owner's decision, 2026-11-11). The factory demo firmware is gone after the
first flash.

## 5. Hito 0 acceptance checklist

Run `scripts/flash-firmware.ps1 -Env hwtest` (or `pio run -e hwtest -t upload` then
`pio device monitor`) and check each item **looking at the screen**:

- [x] 🖥️ Identify SoC, flash and PSRAM size (section 1).
- [ ] ⏳ Confirm display + touch controller with physical test (sections 2, 5).
- [x] 🖥️ Compile and flash minimal example from PlatformIO (`hwtest`, 1.16 MB, flashed to COM6).
- [x] ✅ Screen on, title `PC CONTROL DECK - HARDWARE TEST` readable (owner: "se ve perfecta").
- [x] ✅ Colour bars correct per owner ("se ve perfecta"), left to right: **RED, GREEN, BLUE, WHITE, BLACK** and an amber strip.
      - Red shows as blue → RGB/BGR order wrong (MADCTL bit 3).
      - Red shows near-black / noise colours → byte swap wrong (`LV_COLOR_16_SWAP`).
      - Colours look like a photo negative → inversion (INVON/INVOFF).
- [x] ✅ No artefacts reported by owner.
- [ ] ⏳ Touch: the purple crosshair follows the finger; each red **corner square turns green**
      when touched (validates orientation; if the opposite corner lights up, set
      `PCD_TOUCH_MIRROR_X/Y` or `PCD_TOUCH_SWAP_XY`).
- [ ] ⏳ Backlight slider dims/brightens the panel smoothly; no black screen at boot.
- [x] 🖥️ Wi-Fi scan reports networks (5–6, best −46 dBm, ~2.6 s) while LVGL keeps rendering (log only; UI responsiveness to be confirmed by eye).
- [x] 🖥️ Serial logs show heap internal / PSRAM every 5 s, frame push time and touch stats.

If the panel is backlit but stays black or shows garbage with table 1, rebuild with table 2:

```powershell
$env:PLATFORMIO_BUILD_FLAGS="-DPCD_LCD_INIT_TABLE=2"; pio run -e hwtest -t upload
```

## 6. Results log

| Date | Item | Result | Notes |
|---|---|---|---|
| 2026-11-11 | esptool identification | 🖥️ | see section 1 |
| 2026-11-11 | hwtest boot | 🖥️ | Arduino 3.3.5 / IDF 5.5.1, PSRAM 8192 KB found, CPU 240 MHz, init table 1 |
| 2026-11-11 | full-frame push | 🖥️ | 17.0–17.6 ms per 320×480 frame at 40 MHz QSPI (theory 15.4 ms) |
| 2026-11-11 | memory | 🖥️ | internal heap 214 KB free (min 213 KB, stable over 35 s), PSRAM 7.9 MB free |
| 2026-11-11 | touch | 🖥️ | 0x3B only device; presses detected; corner-target orientation not explicitly confirmed |
| 2026-11-11 | display visual | ✅ | owner reports the hwtest screen looks perfect (init table 1, no byte/colour swap) |
| 2026-11-11 | on-target unit tests | 🖥️ | `pio test -e test`: 7/7 PASSED (history, thermal alerts) |
| 2026-11-11 | deck (Hito 1) | ✅ | owner: demo UI OK; log at 40–50 s: link ONLINE, rx 39→49, 0 dropped events, 17 ms push, heap 210 KB stable |
| 2026-11-11 | serial capture | 🖥️ | use `pio device monitor`; the ad-hoc pyserial capture misses USB-CDC output after flashing |
| 2026-11-11 | USB link | ⚠️ | board dropped off USB 4 times. Correlates with the **Unity test firmware** (and once the factory firmware): the device vanishes from Windows (no PnP/System events) while/after tests run; `hwtest`/`deck` stayed connected for many minutes. Test firmware does not init the LCD/backlight (GPIO1 floating). Under investigation; replug recovers it |
| 2026-11-11 | test_protocol on target | ⏳ | could not complete (USB drop). Parser covered by agent-side fixture tests; to be re-run |
| 2026-11-12 | deep-sleep wake (0.8.0) | ⚠️ | owner: needs ~3 taps to wake. Cause: SLPIN on the TDDI also slows the touch scan; 0.9.0 keeps the panel in normal mode while asleep |
| 2026-11-12 | deep-sleep wake (0.9.0) | ⏳ | expected: one tap wakes (log `woke from deep sleep (ext1 mask 0x8)`); check no instant wake right after `sleep` |
| 2026-11-12 | Crypto tab (0.9.0) | 🖥️ | built and rendered in the desktop simulator (ES/EN); on-device check pending: prices, charts, scrubbing, picker |
| 2026-11-13 | screensaver wallpapers (0.10.0) | ⏳ | rendered in the desktop simulator (ES/EN). On the board: `saver bg 0..7` + `saver`: every image decodes without artefacts or swapped colours (JPEGDEC S3 SIMD, big-endian RGB565); the picker shows 8 thumbnails |
| 2026-11-13 | screensaver fade / crossfade (0.10.0) | ⏳ | Rotate every 1 min: the change is smooth enough with full-frame pushes; the gliding title doesn't make touches sluggish |
| 2026-11-13 | screensaver exit and power (0.10.0) | ⏳ | a touch closes it without pressing what is underneath; BOOT and a thermal alert close it; with it enabled the deck never deep-sleeps by itself |
