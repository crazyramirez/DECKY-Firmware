# Changelog

Firmware for every DECKY deck (Guition JC1060P470C, JC4827W543, JC3248W535, ESP32-4848S040), one version for all
of them. Up to 1.0 each deck had its own repository and versions: their history is in
[JC1060P470C](decks/JC1060P470C/changelog-0.x.md) (up to 0.13.0),
[JC4827W543](decks/JC4827W543/changelog-0.x.md) (up to 0.13.0) and
[JC3248W535](decks/JC3248W535/changelog-0.x.md) (up to 0.14.0).

## firmware 1.4.0 — Battery level on the screensaver

- **Screensaver: the deck's battery, discreetly, in the top right corner** — a small drawn battery
  whose fill follows the charge (amber at 20 % and under, red at 10 %), with the percentage before
  it if you want. Only while the deck runs on its battery: on USB it never shows.
- **Settings › Screensaver › Battery:** *Off · Icon · Icon and %* (default: icon and %).
- Shared by every deck (`core/ui/battery_badge`); the setting appears only on decks that can sense
  a battery (today the JC3248W535, `battery::sensed()`). On the 7" and 4.3" decks the "Touch to
  return" hint moves to the badge's left while it shows.

## firmware 1.3.0 — More cities on Today

- **Every deck: Today can hold up to 6 cities.** Swipe sideways on Today and the whole page is the
  next city's: its local time and date, its weather, forecast and picture (and, on the 7" deck, its
  rain or snow). A pill shows which city it is, how far ahead its clock is (`Tokio · +8 h`) and a
  dot per city; with one city it shows a `+`.
- **Cities sheet** (tap the city on Today, or *Settings › Time & weather › Cities*): a world clock
  with every city's local time, day (*Today / Tomorrow / Yesterday*), time difference, weather and
  temperature. Tap a city to see it on Today. *Add city* opens the city search; *Edit* shows the
  actions: remove a city, make it the main one, or change the main city.
- The **main city** is the one that sets the deck's clock without a PC and shows on the screensaver,
  as before; the others keep their own time zone (summer time included) and are remembered across
  restarts. Every city's forecast is refreshed every 10 minutes, through DECKY Agent when it is
  connected and straight from Open-Meteo otherwise: no new version of the PC app is needed.
- Long city names are cut letter by letter with `…` (LVGL cut at the last whole word and left half
  the room empty).
- USB console: `city add <lat> <lon> <name>`, `city del <n>`, `city main <n>`, `city show <n>`,
  `modal cities`; `weather` lists every city.

## firmware 1.2.2 — No more random restarts after boot

- **Every deck: a restart a few seconds after power-on is gone.** The first web request (weather,
  crypto) could meet the clock's own time-server lookup: Arduino's name lookup cleared the network
  stack's DNS cache without its lock and the deck stopped with a panic (`assert failed:
  udp_new_ip_type ... Required to lock TCPIP core functionality`, read from the core dump). Every
  new Wi-Fi connection now resolves one name first, with the time sync paused.
- **Every deck:** when the screen comes back on with a full-screen sheet open (the screensaver, a
  settings sheet), the whole screen is redrawn; before, the panel could keep the picture it had
  when it went off.

## firmware 1.2.1 — Smoother ESP32-4848S040; no page drawn under full-screen sheets

- **Every deck:** while a full-screen sheet is open (Wi-Fi and its keyboard, place search, pairing,
  the setup assistant, the screensaver) the page underneath is no longer drawn on every refresh:
  LVGL 8 drew it although nothing of it showed. On the ESP32-4848S040 the Wi-Fi sheet with its
  keyboard redraws in about half the time (~215 → ~115 ms for the whole screen).
- **ESP32-4848S040, much lighter small changes:** every refresh copied the whole 480×480 frame to
  the panel (~28 ms, twice with two changed areas) whatever had changed, because LVGL's direct mode
  hands the whole display to the flush; now only what changed is copied (a key, a button, the clock:
  well under 5 ms). The copy also starts at once when the scan is clear of those rows, or as soon as
  it has passed them, instead of waiting for the next frame (up to 24 ms).
- **ESP32-4848S040:** LVGL's drawing loops run from internal RAM (~10–15 % less per redraw). Console
  `bench` times whole-screen redraws; `status` shows the last refresh (render, wait, copy).
- **ESP32-4848S040:** a software restart (console `reboot`, a crash) no longer hangs a few seconds
  until the watchdog resets the chip: the panel's DMA is stopped before restarting.

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
