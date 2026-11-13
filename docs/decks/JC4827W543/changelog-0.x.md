# Changelog before 1.0 — JC4827W543

Firmware history of the Guition JC4827W543 while it had its own repository (PC-Control-Deck-ESP32-JC4827W543), up to
the last separate release. From 1.0.0 every deck is built from this repository: see [CHANGELOG](../../CHANGELOG.md).


## firmware 0.13.0 — Today: a background that follows the weather

- The Today page puts a photograph of the current weather behind the clock and the cards: sunny or
  starry, clouds, fog, drizzle, rain, storm, snow or sleet, by day or by night. Cold days (below
  5 °C) get a blue veil, hot ones (above 30 °C) a warm one.
- *Settings › Time & weather › Weather background* turns it off (the page then keeps the tint of
  the sky, as before). On by default.
- 18 pictures of one alpine valley under every kind of weather (clear, partly cloudy, overcast,
  fog, drizzle, rain, storm, snow, sleet; day and night), so the page changes mood, not place.
  Each is fitted to this deck's Today page and darkened where the text sits on it, so the clock and
  the conditions stay readable. The prompts and the source images live in
  `firmware/tools/weather_backgrounds/`; `firmware/tools/gen_weather_bg.py` embeds them.

## firmware 0.12.4 — "Connecting to <network>…" instead of a false "Could not connect"

- Connecting to a Wi-Fi network (Settings › Wi-Fi, the setup assistant) showed *Could not connect*
  at once and connected a few seconds later. Stopping the radio for the new network reports our own
  disconnect, and a fresh attempt often fails once or twice before it gets in (above all on the
  ESP32-C6 radio of the JC1060P470C). The status now says *Connecting to <network>…* and names a
  reason (wrong password, network not found, could not connect) only when the attempt is still
  failing after 12 s. A dropped connection gets the same grace while it reconnects.

## firmware 0.12.3 — Setup assistant without the coins step

- The setup assistant no longer asks for the Crypto coins: the Crypto tab starts with the default
  ones (BTC, ETH, SOL, XRP, BNB, DOGE); *Settings › Crypto › Coins* changes them. Five steps:
  language, Wi-Fi, PC, city, done.
- The city step needs only Wi-Fi (the search works without a PC since firmware 0.11); it used to
  ask for the PC first.

## firmware 0.12.2 — Board identity for the USB updater

- USB console: new command `id` answers `pcdeck board=JC4827W543 fw=0.12.2`. The PC app (0.8.0 or later)
  uses it to recognise the deck it updates over USB (older firmware is recognised from `status`).

## firmware 0.12.1 — Status LED instead of the status pill

- The status pill (`CONNECTING`, `LIVE`, `NOT PAIRED`…) at the top right becomes an **LED**: green
  with a soft glow while the PC app is connected, grey otherwise. No words; demo mode keeps an amber
  LED with `DEMO`, so synthetic data never passes as real. Tapping the grey LED still opens Wi-Fi
  setup or pairing; *Settings* shows the Wi-Fi address and the paired PC (✓ when connected).

## firmware 0.12.0 — PC tabs appear with the PC; Crypto list in the JC1060P470C style

- **Power-on without the PC tabs.** Monitor, Thermals and Player now join the rail when the PC app
  connects, instead of showing at boot and leaving after a 20 s grace when the PC does not come. If
  the first tab in your order is a PC tab, the deck opens the first tab that works and switches to
  the PC tab when the PC connects (unless you have picked another page meanwhile).
- **Coin cards** like the 7" deck's, at this screen's scale: a 42 px card with a thin border and the
  coin's brand colour as an accent on the left edge, the 32 px logo (was 24), the symbol with its
  **market-cap rank** (`BTC #1`; left out when a long symbol leaves no room) over the name, the 24 h
  sparkline with a soft glow, the price over its **24 h change in currency**, and the 24 h change
  with a caret in a green / red pill with a matching border. Column widths are measured with the
  fonts ("Hyperliquid", a BTC price, "▼ -10,25%").

## firmware 0.11.1 — Crypto header fits every Fear & Greed value

- Crypto, global strip: the change and the Fear & Greed class sit under the value instead of beside
  it, and the gauge moves next to the number. "20" and "Miedo extremo" (or "Codicia extrema", or
  "100") no longer run into each other in a third of the 480 px width.

## firmware 0.11.0 — Works without a PC

- **Today, Crypto and the screensaver no longer need the PC.** While no PC app is connected (none
  paired, or the paired PC is off) the deck fetches the data itself, from the same public services
  the PC app uses: Open-Meteo (forecast every 10 min and the city search), Binance (prices, 24 h
  sparklines, charts), CoinGecko (market cap, rank, ATH, global market) and alternative.me (Fear &
  Greed), over HTTPS with the server certificates checked against the framework's root bundle.
  Prices show up first and the sparklines and market data fill in a moment later. When the PC app
  connects it takes over again. [ADR 0007](adr/0007-without-a-pc.md).
- **The PC tabs follow the PC.** Monitor, Thermals and Player are in the tab bar only while a PC is
  connected (a paired PC gets 20 s after power-on). If the PC goes away while one of its pages is
  open, the deck moves to the first tab that works and returns to that page when the PC comes back.
- **Screensaver:** without a PC it shows only what it has — clock, date, weather and the coin; the
  music card no longer keeps showing the last song once the PC is gone.
- Clock without a PC: SNTP time plus the UTC offset of the weather place (summer time included).
- A paired PC that is off no longer makes the deck rebuild its Wi-Fi association every minute or
  so while the internet answers (that recovery is for a link that passes no traffic at all).
- Messages say *Connect to Wi-Fi* instead of *Connect the PC* for the weather, the city search and
  the prices.
- USB console: `nopc 1|0` ignores the paired PC until reboot (testing). Simulator: a no-PC scene
  (first tab, Crypto, screensaver).

## firmware 0.10.4 — Clear toggles in Settings

Ported from the JC1060P470C deck (0.11.1 and 0.11.4).

- Settings: the selected segment is accent-tinted with an accent hairline and bright text; the
  others are quiet (the old grey-on-grey made on/off hard to tell). The screensaver's *Show*
  chips carry a ✓ while they are on.
- First boot: the NVS namespaces are created, so the read-only opens no longer log
  `nvs_open failed: NOT_FOUND` every second.

## firmware 0.10.3 — Releases: app images only

- The release carries only the deck app: `pcdeck-deck-…` (app and merged). CI no longer builds the hardware test
  (`hwtest`) or compiles the on-target tests; both stay in `platformio.ini` for local use.
- GitHub keeps only the newest release: once it is published, CI deletes the older releases
  and their tags, the earlier workflow runs and this run's artifacts (the release holds the
  same files). No firmware changes.

## firmware 0.10.2 — Screensaver “Show” chips fixed

- *Settings › Screensaver › Show*: switching the weather / PC / music / crypto chips now always
  stores what the chips show. The button matrix reported each tap on press, before it toggled the
  chip, so the saved choice lagged one tap behind and the screensaver left out cards that were on
  (or showed ones that were off). The chips now report on release, after the toggle.

## firmware 0.10.1 — Screensaver cards open their page

- Tapping a screensaver card closes the screensaver and opens what it shows: the **music** card the
  Player, the **crypto** card the chart of the coin it is showing (the Crypto tab if that coin is
  unknown), the **PC** card the Monitor, the **weather** line the Today tab. The cards light up
  while pressed. A tap anywhere else still just closes it (the touch never reaches the page
  underneath); a finger that slides off a card also just closes it.
- Simulator: a tap on each card (and on an empty area) with a shot of the page it opens.

## firmware 0.10.0 — Screensaver

- **Screensaver** (*Settings › Screensaver*, off by default), ported from the JC1060P470C deck and
  laid out for 480×272: after 1 / 2 / 5 / 10 / 30 min without a touch, a full-screen wallpaper with
  an 84 px clock (blinking colon, 24/12 h), the long date and the data you choose: weather (icon,
  temperature, condition, place) and glass cards for the PC (CPU/GPU temperature and load), now
  playing (cover, gliding title, artist) and crypto (watched coins in turn, price and 24 h change).
  Background fixed, random at every start, or rotating every 1 / 5 / 15 / 30 min with a 1.6 s
  crossfade; a picker sheet with thumbnails; brightness 100 / 60 / 30 % while it shows; *Preview*.
  Fades in; the content drifts every minute (burn-in). See [ADR 0006](adr/0006-screensaver.md).
- While enabled it **replaces auto deep sleep**: the deck never sleeps by itself (the auto-sleep
  control is greyed out and says so). A touch (swallowed) or BOOT closes it, and so do a thermal
  alert, turning the screen off and deep sleep; the IO17/18 button closes it and then runs its
  action.
- Eight wallpapers from the JC1060P470C CryptoTracker (gpt-image-2), kept as JPEG sources in
  `firmware/tools/wallpapers` and fitted to 480×272 by `tools/gen_wallpapers.py` (≈ 150 KB);
  decoded with **JPEGDEC 1.8.4** into PSRAM (ESP32-S3 SIMD; full size for the screensaver, ¼ size
  for the picker thumbnails). New `pcd_font_84` (clock digits).
- **Wi-Fi page scans by itself:** every time it is shown (*Settings › Wi-Fi*, the setup assistant,
  the status pill) a scan starts at once and repeats every 15 s while the page stays visible; the
  previous list stays on screen with a spinner until the new results arrive (no scans while the
  page is hidden, under the password sheet, the screensaver or a dark screen).
- Settings: disabled segmented controls are now really dimmed (an LVGL button matrix ignores the
  object's disabled state when it draws its buttons).
- USB console: `saver [now|off|bg <n>]`. Simulator: screensaver, picker, Settings and Wi-Fi
  scanning screenshots (JPEGDEC built with `NO_SIMD` on the PC: its x86-64 SSE path mis-decodes
  these images); C objects are rebuilt when `lv_conf.h` changes.
- Flash: 2.37 MB of the 3 MB app slot (≈ +190 KB: wallpapers, JPEGDEC, clock font, UI); RAM
  unchanged (20.7 % static, the wallpaper buffers are in PSRAM).

## firmware 0.9.0 — Today tab spacing

- Version **0.9.0**, the same number as the JC3248W535 firmware (it replaces the 0.8.1 release).
- **Today** breathes more, with every value kept: the clock is 52 px (was 60; `pcd_font_52`
  replaces `pcd_font_60`) so the date gets ~8 px of air, the "now" card spreads its rows evenly,
  the next-hours card is taller (80 px, 6 px padding) and the next-days rows put 7 px on each side
  of the range bar (the max now sits next to the bar instead of at the far edge).

## Desktop app in its own repository · agent 0.7.0 (Linux and macOS)

- The PC app moved to **[PC-Control-Deck-Agent](https://github.com/crazyramirez/PC-Control-Deck-Agent)**,
  shared with the JC3248W535 deck (protocol fixtures, PROTOCOL / SECURITY docs, agent ADRs 0002,
  0003, 0005). `agent/`, `shared/`, the agent scripts and `agent.yml` were removed from this
  repository; app releases are published there.
- Agent 0.7.0 runs on **Windows, Linux and macOS** (same tray app, protocol and pairing; the firmware
  is unchanged): see the [agent changelog](https://github.com/crazyramirez/PC-Control-Deck-Agent/blob/main/docs/CHANGELOG.md).
- `firmware/tools/gen_fixtures.py` reads the fixtures from a PC-Control-Deck-Agent clone next to this
  repository (or `--agent DIR`); `--check` fails while `fixtures.h` is out of date. `firmware.yml`
  checks out the agent repository for that check.

## firmware 0.8.0 / agent 0.6.0 — Guition JC4827W543 + Crypto

**New board: Guition JC4827W543** (4.3" 480×272 IPS, NV3041A over QSPI, GT911 touch, ESP32-S3 N4R8).
This repository targets that board only; the JC3248W535 version (firmware 0.7.0) lives in
its own repository ([its history](../JC3248W535/changelog-0.x.md)).

- Board layer: own NV3041A QSPI driver (vendor Arduino_GFX init table and framing, manual CS,
  ping-pong DMA bounce buffers, whole-frame pushes like the vendor demos), GT911 driver (reset with
  address selection 0x5D/0x14, resolution scaling), 4 MB flash with `huge_app.csv` (3 MB app),
  backlight on GPIO1. No battery-sense line on this board: only "USB host present" is shown.
- **Landscape UI** (480×272): navigation rail on the left (5 reorderable tabs + Settings), slim top
  bar with a back button for sub-pages, title and a contextual subtitle. Every page re-laid out:
  Monitor (4 cards in a row + chart), CPU/GPU detail and Thermals in two columns, Player with the
  cover on the left, Today in two columns, Wi-Fi / Pair PC in two columns, landscape keyboards and
  keypads.
- **Crypto tab** (ported from the JC1060P470C CryptoTracker): market cap, BTC dominance, Fear &
  Greed; up to 12 coins with live price, 24 h change and sparkline; coin detail with 7 ranges,
  line (glow + gradient) or candles, volume, period high/low, last-price tag, finger crosshair,
  24 h stats, market cap/rank, ATH; swipe between coins. Coin picker (54 coins with logos),
  6 currencies, refresh 15/30/60 s, chart style. Data comes from the PC app (the deck stays LAN-only).
- **Sleep button on IO17/IO18** (TTP223-style touch module): tap = deep sleep, tap again = wake
  (or screen off/on, Settings › System). Idle level learnt per pin; deep-sleep wake sources chosen
  from the idle levels of the button, touch INT and BOOT (ext1 + ext0).
- **Setup assistant**: opens at power-on while no Wi-Fi is stored (and from Settings): language,
  Wi-Fi, pair PC, city, coins; the Wi-Fi / Pair steps open their pages and it returns by itself when
  the step is done.
- Compact type: 10 px navigation-rail labels, 12 px captions with icons, 60 px clock; smaller
  Settings rows, segmented controls and buttons.
- Uploads finish with a watchdog reset: GPIO0 (BOOT strap) shares the panel's TE line, so an RTS
  reset with the panel initialised would land in the ROM download mode.
- Fonts: euro sign at every size, bitcoin / coins / chart / caret icons.
- Serial console: `page crypto`, `coin <SYM>`, `coins <list>`, `crypto`, `modal coins`, 5-tab `tabs`.
- Desktop UI simulator (`firmware/sim`, zig + LVGL): renders every page to PNG without hardware.
- Agent 0.6.0: `crypto.request` → `crypto.data` and `crypto.chart` → `crypto.candles` (Binance
  public market data, CoinGecko, alternative.me; cached, rate-limited, converted to the chosen
  currency). Fixtures `crypto_data.json` / `crypto_candles.json`; deck parser tests for both.

## firmware 0.7.0 / agent 0.5.0

- **Today tab:** big clock (PC time, 24/12 h) and date; current weather (icon, temperature,
  condition, max/min, humidity, wind, feels-like) with a sky-tinted background; next 6 hours; next
  5 days with rain chance and range bars; sunrise/sunset. A call to action when no city is set.
- **Location search** (Settings › Time & weather or the Today tab): city search sheet with the
  on-screen keyboard; the chosen place is stored on the deck. Units °C/°F and 24/12 h clock.
- Agent: Open-Meteo weather + geocoding (`weather.search` / `weather.request` →
  `weather.results` / `weather.data`, cached), PC clock in every snapshot (`clock`).
- Deck: SNTP time before the PC connects, last UTC offset remembered.
- Tabs: Today is a fourth reorderable/hideable tab (older saved orders get it appended); bottom-bar
  labels in a 12 px font so five tabs fit.
- Fonts: weather icons (14–40 px), 72 px clock digits, 12 px labels.

## agent 0.4.7 / firmware 0.6.0 (update 4)

- Album art no longer stays grey: players publish a new track's title before its thumbnail, so the
  agent re-reads the cover every 1.5 s for 12 s after a track change (up to 60 s while there is none).
- New `media.art_request` (deck → agent): if the snapshot names a cover the deck never got, the deck
  asks again (after 2.5 s, then every 5 s) and the agent resends it.
- The cover survives a UI rebuild (language change).

## firmware 0.6.0 (update 3)

- Fix: "Turn off screen" could not be undone. The AXS15231B is a TDDI chip and DISPOFF also stops
  touch scanning; the screen now only switches the backlight off and stops pushing frames. BOOT also
  turns the screen back on.
- Auto sleep (Settings → System): deep sleep after 1 / 2 / 5 / 10 minutes without a touch, or never
  (default). Stored in NVS; a toast warns 10 s before; any touch restarts the countdown.
- Settings → Tabs: an eye button per tab shows or hides Monitor / Thermals / Player in the bottom bar
  (stored; the first visible tab opens at boot; Settings is always shown).
- Player: the volume slider changes the PC volume while dragging (about every 150 ms), not only on release.

## agent 0.4.6

- Online services in a browser tab without media metadata (Instagram, TikTok, Netflix, Twitch, X,
  Prime Video, Disney+, Max, Facebook, Vimeo, SoundCloud, Spotify/Deezer/Tidal/Apple Music web…)
  are recognised from the tab title: generated cover (brand colours + icon + name, drawn locally)
  instead of the browser logo, source shown as "<Service> · <browser>", "- YouTube"-style suffixes
  removed from titles.

## agent 0.4.5 / firmware 0.6.0 (update 2)

- Play only acts on the detected player again (media key when there is none); launching a player
  app from the deck (0.4.3/0.4.4) was removed: too intrusive with other apps.
- Installed web apps show their own name on the deck instead of "Chrome".
- Deck: Settings > Display > Refresh 1/2/5 s (default 5 s) for Monitor/Thermals/detail; data
  still arrives every second (player, history), and the first 6 s after boot/reconnect repaint
  on every snapshot so values appear at once.

## agent 0.4.3 / firmware 0.6.0 (update)

- Sticky player selection: another app only takes over after playing for 3 s; a 4 s grace hold
  during track changes (no more jumps to WhatsApp / other tabs when skipping).
- Play icon optically centred; battery strip text/bar spacing.

## agent 0.4.2 / firmware 0.6.0 — 2026-11-12

- CPU clock and CPU package power without the PawnIO driver: Windows performance counters
  (`% Processor Performance` x `Processor Frequency`, `Energy Meter(RAPL_Package0_PKG)\Power`),
  used only when LibreHardwareMonitor has no value.
- Deck: Settings > System > "Turn off screen" (any touch wakes it, that touch is swallowed) and
  "Deep sleep" (Wi-Fi off, panel SLPIN, ESP32-S3 deep sleep; wake on touch INT GPIO3 or BOOT).
  Thermal alerts turn the screen back on. PC rows renamed "Lock/Restart/Shut down PC".
- Deck battery: USB detection from USB host activity (the battery node follows the cell even on
  USB), hysteresis + debounce; no more "USB" on a full battery.
- CPU/GPU detail renders the current data the moment it opens (it showed the other device for
  ~1 s); missing values are dimmed; the real temperature sensor name is shown.
- Redesigned pairing code (6 digit cells, accent OK key), manual IP entry (own keypad; the old
  floating keyboard covered the fields) and Wi-Fi password sheet (field with lock/eye, styled keyboard).

## agent 0.4.1 / firmware 0.5.1 — 2026-11-12

- Player (was "Control"): album art (150 px RGB565 binary frames), accent-tinted background,
  progress with times, **seek by dragging the progress bar** (`media.seek`, `media.canSeek`),
  long titles/artists scroll as a marquee, silent success for media/volume commands.
- Settings: Lock PC / Restart / Shut down (with confirmation); shutdown/restart only when enabled
  in the PC app ("Permitir apagar y reiniciar"), off by default.
- Fonts: text sizes now include Latin Extended, Cyrillic, Vietnamese and typographic quotes/dashes;
  the agent folds unsupported characters (emoji, CJK, Greek) so the deck never shows empty boxes.
- When nothing plays, the deck stays on the last player heard instead of jumping to another session.
- Demo mode removed from Settings (the deck always boots with real data; `demo on` stays as a
  USB-console developer aid and is not persisted).
- Laptop battery on Monitor: charge %, charging / plugged in / time left, remaining and full
  energy (Wh); hidden on desktops. Metric cards slightly shorter to keep the chart readable.
- Deck battery (GPIO5 divider): top-bar indicator (charge bolt on USB, level + % on battery) and
  Settings > About row with voltage. On USB the ADC sees the ~4.9 V supply, so % is only measurable on battery.
- Settings > About: internal heap and PSRAM on separate rows.
- "Inicio" renamed "Monitor"; Settings > Tabs reorders Monitor / Thermals / Player in the bottom
  bar (stored in NVS, the first tab opens at power-on; USB console `tabs <a> <b> <c>`).
- Compact top bar (smaller title, PC name and status pill), more air between volume and tab bar,
  Wi-Fi screen shows the paired PC name.

## agent 0.3.0 / firmware 0.4.0 — 2026-11-12

- Remote controls work: volume (Core Audio), play/pause/next/previous through the Windows media
  session (any SMTC player incl. browser web players; media-key fallback), session lock. Allow-list,
  value validation, per-device de-duplication and rate limit; boot-nonce request ids on the deck.
- Snapshots carry PC volume/mute and "now playing" (title, artist, app, state); deck Controls shows
  them and keeps the volume slider in sync.
- Lenovo Legion 2023+ EC sensors via `LENOVO_OTHER_METHOD` capability IDs (CPU temp, fans), raw
  values logged at start-up for verification.
- PC app window sizes itself exactly to its content.

## agent 0.2.0 / firmware 0.3.0 — 2026-11-12

- Pairing with a one-time code + per-device token auth (DPAPI on the PC, NVS on the deck); mDNS
  discovery (`_pcdeck._tcp`) and automatic re-location of a paired PC whose IP changed.
- Tray app (PySide6): status, pairing code with countdown, paired devices with unpair, this-PC info,
  start with Windows. Inno Setup installer with elevated scheduled tasks and private-only firewall rules.
- CPU temperature from the Lenovo Legion EC (GameZone WMI) when elevated; `temps` list of extra sensors.
- Deck: "Pair PC" screen (PC list + big keypad), status pill opens Wi-Fi/pairing, dynamic thermals list,
  dashboard cards degrade to load when temperature is missing, GPU power fallback (labelled), chart refresh fix,
  Wi-Fi watchdog, USB `shot` screenshot and `page` console commands.

## 0.2.0-hito3 — 2026-11-11 (unreleased)

- Agent switched to Python 3.13 (ADR 0002): LibreHardwareMonitor 0.9.6 via pythonnet, sensor
  normalizer (null never 0, CPU 0 W -> null, stale detection, discrete-GPU selection), catalog +
  redacted diagnostics, WebSocket server (read-only, LAN-only peers, hello/rate/size limits).
- Protocol v1 documented (PROTOCOL.md, now in the agent repository) with shared fixtures used by agent and firmware tests.
- Firmware: network task (Wi-Fi + WebSocket, backoff with jitter), USB serial provisioning console,
  protocol parser (ArduinoJson 7.4.3, WebSockets 2.7.3), agent capabilities/notes in Settings.

## 0.1.0-hito1 — 2026-11-11 (unreleased, not yet validated on hardware)

- Repository skeleton, ADR 0001 (firmware stack), hardware validation document.
- Firmware `hwtest` env: own AXS15231B QSPI driver (2 selectable init tables), AXS15231B touch
  over I²C, PWM backlight, LVGL 8.4.0 direct-mode port, validation screen, I²C scan, Wi-Fi scan,
  heap/PSRAM logging.
- Firmware `deck` env: dashboard (CPU/GPU/RAM/CPU-package-power cards + 60 s chart), thermals
  (4 sensors, CPU/GPU history, 60 s max), CPU/GPU detail, controls (volume, media, lock,
  confirm-gated shutdown/restart with ACK/NACK/timeout), settings (brightness, ES/EN, alerts,
  demo mode, pause feed, diagnostics). Demo source on its own task through the event bus.
- Thermal alerts with hysteresis, minimum duration and cooldown; never act on the PC.
- On-target Unity tests for alerts and history (`pio test -e test`).
