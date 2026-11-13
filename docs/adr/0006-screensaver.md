# ADR 0006 — Screensaver: wallpapers, JPEG decoding, data cards

Status: accepted (firmware 0.11.0). Shared design for the three decks (JC1060P470C, JC4827W543,
JC3248W535); each repository adapts the layout to its screen.

## Context

The owner wants every deck to have an independent screensaver mode: a chosen, random or rotating
background, the date and time, the temperature and other data, a configurable delay, and — while
it is enabled — no auto sleep. Eight wallpapers already exist: the JC1060P470C CryptoTracker made
them with gpt-image-2 (1024×600, dark, a calm upper band for a big clock, a darker lower band) and
embedded them as JPEG for the ESP32-P4 hardware decoder, with LVGL 9 thumbnails.

## Decisions

1. **One source of truth for the images:** the eight JPEGs live in `firmware/tools/wallpapers`
   (+ `wallpapers.json` with the Spanish / English names). `tools/gen_wallpapers.py` fits them to
   the deck's screen ("cover": scaled and centre-cropped — portrait on the 3.5" deck) and embeds them
   as baseline JPEG in `src/assets/wallpapers.c`. The header has no LVGL types, so the same format
   works with LVGL 8 on every deck. No thumbnails are stored.
2. **JPEGDEC 1.8.4** (bitbank2, Apache-2.0) decodes into RGB565 buffers in PSRAM, in LVGL's byte
   order (`LV_COLOR_16_SWAP`), shown as `LV_IMG_CF_TRUE_COLOR` images. It is portable C (and uses
   the ESP32-S3 SIMD on the QSPI decks), so the three decks share one decoder; the P4's hardware JPEG
   engine would be faster but only exists on one board. Its ¼-scale decode makes the picker
   thumbnails on the fly. The decoder state (~20 KB) is allocated in PSRAM only while decoding.
   (In the desktop simulator JPEGDEC is built with `NO_SIMD`: its x86-64 SSE path mis-decodes these
   streams; the devices never take that path.)
3. **Top-layer overlay**, created on start and deleted on stop (nothing allocated while off except
   the two cached wallpaper buffers). Two image objects allow a crossfade (opacity animation of the
   incoming image: no layer rendering needed). A 20 % black shade keeps white type readable; data
   sits on frosted-glass cards (dark translucent fill, hairline border).
4. **Content:** big clock (digits-only font generated for it) with a blinking colon, long date,
   weather line, and up to three cards — PC (CPU/GPU temperature and load), now playing (album art
   reused from the Player, gliding title), crypto (the watched coins in turn every 8 s, from the
   same store as the Crypto tab: in the background the PC app is asked every 2 minutes). Cards
   without data are hidden and the row stays centred.
5. **Power interplay:** while enabled the screensaver **replaces auto sleep** (Settings greys the
   auto-sleep control). It starts after N minutes of LVGL inactivity, never over the setup
   assistant or a dark screen. A touch closes it on release (the overlay swallows it), BOOT closes
   it, a thermal alert closes it, turning the screen off closes it. Its brightness is a percentage
   of the normal one.
6. **Burn-in:** the content drifts randomly within ±18 px every minute; the colon blinks; rotation
   changes the background.
7. **Settings:** one NVS-backed `storage::Screensaver` struct (enabled, delay, background mode,
   image, rotation period, shown data bitmask, brightness), validated as a whole.

## Consequences

- Flash: ≈ 0.5 MB of JPEG at 1024×600 (less on the smaller decks), the clock font, JPEGDEC.
- PSRAM: two screen-sized RGB565 buffers while the screensaver has run (1.2 MB each at 1024×600).
- Decoding a full wallpaper blocks the UI task briefly (start / rotation), when nothing else is
  being touched.
