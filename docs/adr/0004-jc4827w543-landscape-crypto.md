# ADR 0004 — Port to the Guition JC4827W543: landscape UI, Crypto tab, sleep button

Status: accepted (firmware 0.8.0, agent 0.6.0). Supersedes the board-specific parts of ADR 0001
(AXS15231B driver, portrait layout); the stack (Arduino-ESP32 3.3.5 / IDF 5.5.1, LVGL 8.4.0,
direct mode) is unchanged.

## Context

The deck moves from the 3.5" portrait JC3248W535 (AXS15231B, 320×480, 16 MB flash) to the 4.3"
JC4827W543 (480×272). Sources: the vendor package mirrored in
`github.com/OldNKrusty/JC4827W543-ESP32S3-Guide` — demo sketches, Arduino_GFX 1.4.4, TouchLib, the
IO pin spreadsheet and the factory binaries. That guide's README describes a 24-bit RGB panel: it
belongs to the Sunton ESP32-4827S043; the vendor demos for this board drive an **NV3041A over QSPI**
(`Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39)` + `Arduino_NV3041A(..., ips=true)`), with GT911 touch on
SDA 8 / SCL 4 / INT 3 / RST 38 and the backlight on GPIO1. The factory binary header and flash id
(`0x4016`) say **4 MB flash**; the module is an ESP32-S3-WROOM-1 N4R8 (octal PSRAM).

## Decisions

1. **One board per repository.** The UI is redesigned for landscape; keeping both layouts would
   double every screen. The JC3248W535 version (firmware 0.7.0) stays in its own repository (merged back in firmware
   1.0: [ADR 0008](0008-one-firmware-repository.md)).
2. **Own NV3041A driver on `spi_master`**, mirroring the vendor-verified Arduino_GFX framing
   exactly: 0x02 + `cmd << 8` for commands with one parameter byte; CASET/RASET parameters "split"
   (command alone, then one 0x32 / 0x003C00 transaction per byte, as `Arduino_NV3041A` does);
   RAMWR as a command, then the pixels as 0x32 / 0x003C00 on four lines with **CS held low for the
   whole transfer** (manual CS, continuation chunks with zero-length command/address phases), 32 MHz,
   SPI mode 0, INVON after the table (IPS). Switches: `PCD_LCD_SPLIT_PARAMS` (0 = parameters in one
   0x02 transaction), `PCD_LCD_PARTIAL_ROWS` (1 = push only the dirty row band).
3. **Whole-frame pushes by default.** The vendor demos only ever flush whole frames
   (`Arduino_Canvas`); partial windows are unproven on this panel. *Tried in 1.2.2 with the real dirty
   rows (LVGL's invalid areas: in direct mode the flush itself always names the whole display): the
   panel flashes and flickers with partial windows, so whole frames stay.* 480×272×2 = 255 KiB ≈ 16 ms at
   32 MHz QSPI, through two 15 KiB internal DMA bounce buffers (frame in PSRAM, LVGL direct mode).
4. **GT911** polled (status 0x814E, first point 0x814F, status cleared after each read). The reset
   drives INT low while RST rises (address 0x5D); 0x14 is probed as a fallback. Coordinates are
   scaled when the controller is configured for another resolution; swap/mirror stay compile-time.
5. **Landscape layout:** 64 px navigation rail (6 items × ~45 px, icon + 12 px label) instead of a
   bottom bar; a 30 px top bar carries the title, a contextual subtitle, the back button of
   sub-pages and the status pill, so no page needs its own header row. Content area 416×242.
6. **Crypto data through the PC app**, not from the deck: the project's rule is that the deck talks
   only to the PC on the LAN (weather already works this way). The agent fetches Binance public market
   data, CoinGecko and alternative.me, caches, converts to the chosen currency and answers
   `crypto.data` / `crypto.candles` (PC-Control-Deck-Agent `docs/PROTOCOL.md`). The deck keeps the coin catalog (54 coins, logos
   generated at 24/32 px in LVGL 8 RGB565A8, big-endian colour plane for `LV_COLOR_16_SWAP`), the
   watch list (symbols in NVS), and renders charts with the CryptoTracker's software rasteriser
   into RGB565 canvases (anti-aliased line, glow, dithered gradient fill, candles, volume).
7. **Sleep button on IO17/IO18** (RTC-capable pins on the S3, free UART1 header): both pins with
   pull-down, idle level learnt at cold boot (kept in RTC memory across deep sleep), a press is the
   opposite level for 40 ms after ≥ 250 ms idle. In deep sleep, ext1 takes every wake line whose
   active level matches the button's, ext0 the first line of the other level (touch INT / BOOT), so
   the GT911 INT polarity (config dependent) cannot cause an instant wake.
8. **No battery gauge:** the pin map has no battery-sense line; `PCD_PIN_BAT_ADC = -1` keeps only the
   USB-host indicator, the old divider logic stays available behind the switch.
9. **Desktop simulator** (`firmware/sim`): the UI, LVGL and the crypto store compiled for Windows
   with zig, hardware/network stubbed, every page rendered to PNG. Used to verify the landscape
   layouts without hardware and for the README screenshots.

## Consequences

- Flash use ≈ 2.2 MB of the 3 MB app slot (fonts + 54 coin logos ≈ 0.5 MB); no OTA slot.
- Items that need the physical board are tracked in HARDWARE_VALIDATION.md (panel colours and
  orientation, touch mapping, GT911 address/INT polarity, TE on GPIO0, sleep-button wake).
