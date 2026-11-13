# Architecture

DECKY is a touch deck next to the PC. The deck runs this firmware; the PC runs
[DECKY Agent](https://github.com/crazyramirez/DECKY-Agent), which reads the sensors, the media
session, the weather and crypto prices, and talks to the deck over the local network (WebSocket,
paired with a 6-digit code; protocol in
[PROTOCOL.md](https://github.com/crazyramirez/DECKY-Agent/blob/main/docs/PROTOCOL.md)). Without the
PC the deck still shows Today, Crypto and the screensaver, fetching the data itself
([ADR 0007](adr/0007-without-a-pc.md)).

## One source tree, several decks

```
firmware/
  platformio.ini        one environment per deck (+ JC1060P470C_r3, hwtest_<DECK>, test)
  src/core/             shared by every deck
  src/decks/<DECK>/     one folder per deck
  include/pcd_lv_mem.h  LVGL heap in PSRAM (every deck)
  boards/, lib/, partitions/   PlatformIO board files, the JD9165 panel driver, the 7" partitions
  sim/                  desktop simulator (zig): sim/stubs.cpp shared, sim/decks/<DECK>/main.cpp scenes
  tools/                generators, all with --deck <DECK>|all; tools/decks.py lists the decks
  test/                 on-target unit tests (pio test -e test)
```

**Every deck builds `src/core` + `src/decks/<DECK>`.** Both are on the include path, so
`#include "app/app_controller.h"` resolves to whichever folder holds the file. **A path lives in
exactly one of the two** — never a copy in core and another in a deck. What decides where a file
goes:

| `src/core/` (shared) | `src/decks/<DECK>/` (per deck) |
|---|---|
| app logic: `app/` (controller, state store, events, demo data, `version.h`) | `include/`: `deck_config.h`, `board_pins.h`, `lv_conf.h` |
| network task, protocol parser, web source (weather / crypto without a PC), serial console | `board/`: panel, touch (except GT911), LVGL port; `app/power.*` (screen off, deep sleep, buttons) |
| storage (settings, Wi-Fi / pairing), alerts, telemetry, crypto store and watchlist | `ui/`: theme, screens laid out for the panel, `ui_root`, `ui_internal.h`, fonts |
| `ui/`: i18n table, chart renderer, wallpaper decoder, crypto formatting, `ui.h` | `ui/i18n_deck.cpp`: the deck's own wording for some keys |
| `board/`: backlight, battery, console I/O, GT911 touch (decks with `PCD_TOUCH_GT911`) | `app/deck_hooks.cpp`: console status lines and deck-only commands |
| `app/deck_hooks.h` (the hooks every deck implements) | `assets/` (wallpapers, weather backgrounds), `crypto/coin_catalog.c`: generated |

The screens stay per deck on purpose: a 7" landscape page with a navigation rail, a 4.3" one and a
3.5" portrait page with a bottom tab bar are different layouts, not one layout scaled.

### What the shared code knows about a deck

`src/decks/<DECK>/include/deck_config.h` — capabilities, checked with `#if` in core:

| Macro | Meaning |
|---|---|
| `PCD_DECK_KEY` | model id: console `id` (`pcdeck board=<KEY> fw=<ver>`), boot log; DECKY Agent's USB updater matches it |
| `PCD_WIFI_ESP_HOSTED` | Wi-Fi is an ESP32-C6 behind ESP-Hosted (JC1060P470C): never `WIFI_OFF`, P4 base MAC, scan retries |
| `PCD_HAS_DEEP_SLEEP` | the deck wakes from deep sleep: auto sleep uses it (else the screen turns off) |
| `PCD_HAS_SLEEP_BUTTON` | IO17/IO18 sleep button (JC4827W543) |
| `PCD_USB_CONSOLE` | console through `board/usb_console` instead of Arduino `Serial` (ESP32-P4) |
| `PCD_TOUCH_GT911` | the shared GT911 driver is built |
| `PCD_BACKLIGHT_HZ`, `PCD_COIN_LOGO_S/_M` | backlight PWM frequency, coin logo sizes |

Pins are in `board_pins.h` (`PCD_LCD_H_RES` / `_V_RES` included); LVGL settings (byte order
`LV_COLOR_16_SWAP`, buffers) in `lv_conf.h`. The tools and the simulator read their screen size and
byte order from these same headers.

### Texts

`src/core/ui/i18n.{h,cpp}`: one table (Spanish / English) for every deck. A deck that needs other
wording — a shorter label for a narrow panel, a hint naming its own buttons — returns it from
`ui::deck_tr()` in its `ui/i18n_deck.cpp`; every other key keeps the shared text.

## Runtime

```
 network task (core 0)                          UI task (Arduino loop)
┌────────────────────────────┐  app::Event    ┌──────────────────────────────────────────┐
│ Wi-Fi, mDNS discovery,     │──── queue ────▶│ app_controller: drains events            │
│ WebSocket to DECKY Agent,  │                │   state_store: histories, alerts, link   │
│ weather / crypto (PC or    │◀── commands ───│   commands: requestId, timeout           │
│ straight from the web)     │                │ ui::*: the only place that calls lv_*    │
└────────────────────────────┘                │ lvgl_port: dirty areas -> panel          │
                                              └──────────────────────────────────────────┘
```

- **Threading rule:** every `lv_*` call happens in the UI task; other tasks post `app::Event`s.
- **Display paths:** JC1060P470C renders 40-line bands into internal RAM and copies them with DMA2D
  into the JD9165 DPI frame buffer ([ADR 0005](adr/0005-jc1060p470c-esp32-p4.md)); the S3 decks use
  LVGL direct mode into a PSRAM frame pushed over QSPI.
- **Data rules:** missing metrics are NaN end to end and render as `--`; synthetic (demo) data is
  marked.
- **Persistence:** settings in NVS namespace `pcd`; Wi-Fi and pairing in their own namespace.

## Adding a deck

1. `src/decks/<KEY>/`: `include/deck_config.h`, `board_pins.h`, `lv_conf.h`; `board/` drivers and
   `lvgl_port`; `app/power.*`, `app/deck_hooks.cpp`; `ui/` (theme, screens, `ui_root`,
   `ui_internal.h`, `i18n_deck.cpp`); start from the deck whose screen is closest.
2. `firmware/platformio.ini`: a `[deck_<KEY>]` section and `env:<KEY>` / `env:hwtest_<KEY>`.
3. `firmware/tools/decks.py` (`TOOLS`) and `tools/gen_fonts.py` (`FONTS`); run the generators with
   `--deck <KEY>` (fonts, wallpapers, weather backgrounds, coin logos).
4. `firmware/sim/decks/<KEY>/main.cpp` (scenes), then `python firmware/sim/build.py <KEY>`.
5. The CI matrix and the "every deck's images" check in `.github/workflows/firmware.yml`, the table
   in `.github/release-notes.md`, and the board catalogue of DECKY Agent's USB updater
   (`flasher/boards.py`).

Decisions: [ADR 0001](adr/0001-firmware-stack.md) firmware stack ·
[0004](adr/0004-jc4827w543-landscape-crypto.md) landscape crypto ·
[0005](adr/0005-jc1060p470c-esp32-p4.md) ESP32-P4 · [0006](adr/0006-screensaver.md) screensaver ·
[0007](adr/0007-without-a-pc.md) without a PC · [0008](adr/0008-one-firmware-repository.md) one
firmware repository.
