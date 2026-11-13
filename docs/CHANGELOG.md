# Changelog

Firmware for every DECKY deck (Guition JC1060P470C, JC4827W543, JC3248W535, ESP32-4848S040), one version for all
of them. Up to 1.0 each deck had its own repository and versions: their history is in
[JC1060P470C](decks/JC1060P470C/changelog-0.x.md) (up to 0.13.0),
[JC4827W543](decks/JC4827W543/changelog-0.x.md) (up to 0.13.0) and
[JC3248W535](decks/JC3248W535/changelog-0.x.md) (up to 0.14.0).

## firmware 1.2.0 — New deck: Guition ESP32-4848S040 (4" square)

- **A fourth deck: the Guition ESP32-4848S040**, a 4" 480×480 square panel (the "86 box" format)
  with an ESP32-S3: every page, the screensaver, the setup wizard and the double tap on the status
  LED, laid out for the square with a bottom tab bar ([its page](decks/ESP32_4848S040.md)). The
  player puts the cover on the left and the controls beside it; the coin picker shows four coins
  per row. Its release files: `pcdeck-ESP32_4848S040-1.2.0[-merged].bin`.
- The panel (ST7701S on the S3's 16-bit RGB bus) is scanned from a frame buffer in PSRAM. LVGL
  draws into a frame of its own and the finished areas are copied to the panel's, timed off VSYNC:
  no flashes while typing or pressing buttons, no tearing on page changes.
- Its USB-C is a CH340 USB-UART bridge: the console and DECKY Agent's USB updater work through it
  (DECKY Agent 1.1 recognises the bridge). No deep sleep on this board: auto sleep turns the screen
  off, a touch turns it back on.
- **Every deck: the setup assistant runs at full brightness** (a first power-on is often in a bright
  room, before any level was chosen). The level set in *Settings* comes back when it closes.

## firmware 1.1.0 — Double-tap the status corner for the screensaver

- **Screensaver on demand, on every deck:** a double tap on the status corner at the top right
  (the deck's battery or charge bolt and the PC link LED) starts the screensaver at once, when it
  is enabled in *Settings › Screensaver*. With the screensaver off nothing changes.
- A single tap keeps its job: on the grey LED it still opens Wi-Fi setup or pairing (while the
  screensaver is enabled, after a short pause to tell it from a double tap). The whole corner now
  answers the tap, the LED itself included.

## firmware 1.0.0 — DECKY: one firmware for every deck

- **New name: DECKY.** The deck says *DECKY* where it said *PC Control Deck* (the JC3248W535's top
  bar, the name the deck announces to the PC: *DECKY 1A2B*) and the pairing steps name the desktop
  app by its new name, **DECKY Agent**.
- **One firmware, one version, one release** for the three decks, built from one source tree
  ([ADR 0008](adr/0008-one-firmware-repository.md)). The release carries every deck's images,
  named after the deck: `pcdeck-JC1060P470C-1.0.0-merged.bin`, `pcdeck-JC1060P470C_r3-…`,
  `pcdeck-JC4827W543-…`, `pcdeck-JC3248W535-…` (DECKY Agent 1.0 picks the right one by itself).
- Everything the decks had in common now runs the same code. On screen nothing else changes; the
  differences that remain are each panel's layout and each board's hardware.
- **USB console:** `status` and `saver` answer alike on every deck (the screensaver command now
  prints its state on the S3 decks too); `help` lists the deck's own commands (`dbg` on the
  JC1060P470C, `sleep` on the S3 decks, `setup` on the JC3248W535); a screenshot (`shot`) stops
  cleanly when the PC stops reading on every deck.
- JC3248W535: the setup wizard no longer opens in demo mode (developer aid), as on the other decks.
