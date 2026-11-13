# DECKY firmware

One PlatformIO project builds the firmware of every DECKY deck. Product overview, flashing and the
console: [README](../README.md). Design: [ARCHITECTURE.md](../docs/ARCHITECTURE.md), stack and pinned
versions: [ADR 0001](../docs/adr/0001-firmware-stack.md), why one repository:
[ADR 0008](../docs/adr/0008-one-firmware-repository.md).

Pinned: pioarduino 55.03.35 (Arduino-ESP32 3.3.5 on ESP-IDF 5.5.1), LVGL 8.4.0, ArduinoJson
7.4.3, WebSockets 2.7.3, JPEGDEC 1.8.4.

## Environments

| Env | Purpose |
|---|---|
| `JC1060P470C` | 7" 1024×600, ESP32-P4 silicon v1.x (every board shipped so far) + ESP32-C6 Wi-Fi |
| `JC1060P470C_r3` | the same for ESP32-P4 silicon v3.x |
| `JC4827W543` | 4.3" 480×272, ESP32-S3 |
| `JC3248W535` | 3.5" 320×480 portrait, ESP32-S3 |
| `ESP32_4848S040` | 4" 480×480 square, ESP32-S3 (console and flashing through a CH340) |
| `hwtest_<KEY>` | hardware bring-up: colour bars, touch corners, backlight, Wi-Fi scan, heap logs |
| `test` | Unity tests of pure logic (alerts, history, protocol), executed **on an ESP32-S3 deck** |

```powershell
pio run -e JC3248W535 -t upload          # build and flash one deck
pio device monitor -e JC3248W535         # serial log / console, 115200
pio run -e hwtest_JC3248W535 -t upload   # hardware test
pio test -e test --upload-port COM6      # on-target unit tests
pio run                                  # JC1060P470C, JC4827W543, JC3248W535, ESP32_4848S040 (default_envs)
```

Helper from the repository root: `scripts/flash-firmware.ps1 -Env JC3248W535 -Port COM6 -Monitor`.
On Windows run PlatformIO from PowerShell, cmd or VS Code, not Git Bash (pioarduino's toolchain
installer refuses MSYS shells).

## Core and decks

```
src/core/            shared by every deck: app logic, network task, protocol, web source, console,
                     storage, alerts, telemetry, crypto store, i18n table, chart and wallpaper code
src/decks/<KEY>/     one deck: include/ (deck_config.h, board_pins.h, lv_conf.h), board/ (panel,
                     LVGL port, touch), app/ (power, deck_hooks.cpp), ui/ (theme, screens, fonts),
                     assets/ and crypto/coin_catalog.c (generated)
include/pcd_lv_mem.h LVGL heap in PSRAM (every deck)
boards/, lib/, partitions/   board definitions of the S3 decks, Espressif's JD9165 panel driver
                     (Apache-2.0), the JC1060P470C partition table
```

Every deck builds `src/core` plus its own folder (`build_src_filter`), and both are on the include
path, so `#include "app/app_controller.h"` resolves to whichever of the two holds the file. The rule:
**a path lives in exactly one of core or the deck folder** — never a copy in each. The screens stay
per deck on purpose: each panel has its own layout.

### What core knows about a deck

Core never tests deck names. Differences it must know are capabilities in
`src/decks/<KEY>/include/deck_config.h`:

| Macro | JC1060P470C | JC4827W543 | JC3248W535 | ESP32_4848S040 | Meaning |
|---|:---:|:---:|:---:|:---:|---|
| `PCD_DECK_KEY` | `"JC1060P470C"` | `"JC4827W543"` | `"JC3248W535"` | `"ESP32_4848S040"` | model id: console `id`, boot log; DECKY Agent's USB updater matches it |
| `PCD_WIFI_ESP_HOSTED` | 1 | 0 | 0 | 0 | Wi-Fi is an ESP32-C6 behind ESP-Hosted: never `WIFI_OFF`, P4 base MAC, scan retries |
| `PCD_HAS_DEEP_SLEEP` | 0 | 1 | 1 | 0 | the deck can wake from deep sleep; without it auto sleep only turns the screen off |
| `PCD_HAS_SLEEP_BUTTON` | 0 | 1 | 0 | 0 | sleep button on the IO17/IO18 header, action in Settings |
| `PCD_USB_CONSOLE` | 1 | 0 | 0 | 0 | console through `board/usb_console` instead of Arduino `Serial` |
| `PCD_TOUCH_GT911` | 1 | 1 | 0 | 1 | the shared GT911 driver (`src/core/board/touch_gt911.*`) is built |
| `PCD_BACKLIGHT_HZ` | 20000 | 5000 | 5000 | 150 | backlight PWM frequency (the ESP32-4848S040's driver goes dark below ~50 % at 5 kHz) |
| `PCD_COIN_LOGO_S` / `_M` | 36 / 48 | 24 / 32 | 24 / 32 | 24 / 32 | coin logo sizes (px) |

Pins (including `PCD_LCD_H_RES` / `_V_RES`) are in `board_pins.h`, LVGL settings (byte order
`LV_COLOR_16_SWAP`, buffers) in `lv_conf.h`. Board-only console output and commands go through
`src/core/app/deck_hooks.h`, implemented in each deck's `app/deck_hooks.cpp`; a deck's own wording
for some texts comes from `ui::deck_tr()` in its `ui/i18n_deck.cpp`.

### Threading

- Only the UI task (Arduino `loop()`) touches LVGL. The network task and the demo source post
  `app::Event`s to a bounded queue that the UI task drains.
- Display: the JC1060P470C renders 40-line bands in internal RAM, copied by DMA2D into the JD9165
  frame buffer in PSRAM; the S3 decks use LVGL direct mode into a PSRAM frame, streamed to the panel
  over QSPI through internal DMA bounce buffers (JC4827W543, JC3248W535) or copied at VSYNC into the
  frame buffer the RGB panel scans (ESP32-4848S040).

### Demo mode

`demo on` on the console feeds synthetic data instead of the PC (developer aid), always marked:
amber `DEMO MODE · synthetic data` text, `DEMO` status pill, simulated command answers tagged
`(simulated)`. Connecting to a Wi-Fi network or pairing from the deck turns it off.

## Simulator

`sim/` builds one deck's UI (LVGL, its screens and the core they use) for Windows with zig, stubs
hardware, network and storage (`sim/stubs.cpp`), and renders every scene of
`sim/decks/<KEY>/main.cpp` to PNG at the deck's resolution.

```powershell
pip install ziglang==0.13.0 pillow
pio pkg install -e JC1060P470C            # once per deck: LVGL and JPEGDEC in .pio/libdeps/<KEY>
python sim/build.py JC1060P470C           # Spanish -> sim/out/JC1060P470C/es/*.png
python sim/build.py JC4827W543 en         # English
python sim/build.py all                   # every deck, both languages
```

`PCD_SIM_TIME=<unix seconds>` pins the clock (reproducible screenshots); `PCD_ZIG_PYTHON` points at
another interpreter if ziglang is installed elsewhere. Adding a deck to the simulator: its
`sim/decks/<KEY>/main.cpp`.

## Tools

Run from the repository root. The generators take `--deck <KEY>` or `--deck all` and read screen
size, byte order and logo sizes from the deck's own headers; what only the tools need (JPEG
quality, where Today's text sits on the weather picture) is in `tools/decks.py`.

| Tool | Output |
|---|---|
| `gen_fonts.py` | `src/decks/<KEY>/ui/fonts/pcd_font_*.c` with lv_font_conv (needs Node.js); type scale per deck in the script |
| `gen_wallpapers.py` | `src/decks/<KEY>/assets/wallpapers.c` from `tools/wallpapers/*.jpg`, fitted to the screen (Pillow) |
| `gen_weather_bg.py` | `src/decks/<KEY>/assets/weather_bg.c` from `tools/weather_backgrounds/*.jpg` (9 kinds of weather × day / night), fitted to the Today page and darkened under its text |
| `gen_coins.py` | `src/decks/<KEY>/crypto/coin_catalog.c` and `src/core/crypto/coin_catalog.h`: 54 coins, logos from CoinGecko |
| `gen_weather_images.py` | redraws the weather source pictures from `tools/weather_backgrounds/PROMPTS.md` (OpenAI Images API, `OPENAI_API_KEY`); only to redo or add one |
| `gen_fixtures.py` | embeds DECKY Agent's protocol fixtures (`shared/fixtures/*.json`) into `test/test_protocol/fixtures.h`; `--check` verifies (CI) |
| `screenshot.py COMx [out.png]` | grabs the deck's current screen over USB (console `shot`) |

`gen_fixtures.py` reads the agent from a clone next to this repository (`../PC-Control-Deck-Agent`),
`--agent <dir>` or the `PCDECK_AGENT_DIR` environment variable.

## Tests

`test/test_logic` (history, thermal alerts) and `test/test_protocol` (protocol parser against the
fixtures of DECKY Agent) run **on the board** with Unity — no host compiler needed:
`pio test -e test --upload-port COMx` with any ESP32-S3 deck connected. CI builds only the four
product environments; run the tests locally.
