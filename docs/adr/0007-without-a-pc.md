# ADR 0007 — Without a PC: direct internet data and PC tabs that follow the PC

Status: accepted (firmware 0.12.0). Shared by the three decks (JC1060P470C, JC4827W543, JC3248W535):
`network/web_source.*` and the `net_task` changes are identical; each UI adapts its tab bar / rail.

## Context

Weather (Open-Meteo), the city search and crypto (Binance, CoinGecko, alternative.me) reached the
deck only through the PC app. Without a connected PC the Today and Crypto tabs and the screensaver
showed *Connect the PC*, although none of that data has anything to do with the PC. The owner wants
the deck useful on its own: Today and Crypto live without a PC, the PC pages out of the way, and a
screensaver that shows only what it has.

## Decisions

1. **The deck fetches the data itself while no PC app is connected** (none paired, the paired PC
   off or unreachable, pairing refused). Same services, endpoints, refresh periods and caches as the
   PC app (`pcdeck_agent/weather.py`, `crypto.py`), filled into the same structures
   (`telemetry::Weather`, `telemetry::Place`, `crypto::Market`, `crypto::Chart`), so the UI does not
   know where the data came from. When the PC app connects it takes over again.
2. **HTTPS with verified certificates:** `NetworkClientSecure` + the framework's mbedTLS root
   certificate bundle (`_binary_x509_crt_bundle_start`, the IDF default list). The bundle is
   attached before every request: `NetworkClientSecure::stop()` wipes the TLS context with it.
3. **One TLS connection at a time** (mbedTLS keeps its buffers in internal RAM), kept open while the
   next request goes to the same host — a price refresh plus its sparklines use one Binance
   connection — and closed after 8 s idle. The network task stack grows to 16 KB for the handshake.
   JSON documents use a PSRAM allocator; Binance klines are parsed by hand (no document).
4. **Progressive crypto:** prices (one batch ticker request + the currency rate) are posted at once;
   then one slower refresh per network-loop pass (a sparkline, market caps, global market, Fear &
   Greed), re-posted at most every 1.5 s. A failed refresh keeps the last good prices and their age.
5. **Clock:** SNTP gives UTC; without a PC the UTC offset of the weather place (Open-Meteo
   `utc_offset_seconds`, summer time included) replaces the PC's.
6. **PC tabs follow the PC:** `AppState::pc_present` (demo mode, or a live or stale link). The bar
   hides Monitor, Thermals and Player otherwise (`app::hidden_tabs()` = the user's hidden tabs +
   those), **from power-on**: they appear when the PC app connects rather than vanishing when it
   does not (until firmware 0.12.0 a paired PC got 20 s to connect, so the deck booted with tabs
   that then went away). An open PC page gives way to the first tab that works and comes back when
   the PC does, unless the user moved meanwhile; at power-on, a PC tab first in the user's order is
   opened when the PC connects, under the same rule.
7. **Screensaver:** the PC card needs a live link (as before), the music card a live or stale one;
   the weather and the coin come from either source.

## Consequences

- The privacy statement changes: without a PC the deck itself contacts Open-Meteo, Binance,
  CoinGecko and alternative.me (the same data the PC app sends: city, coordinates, coin pairs,
  currency). With the PC app connected the deck still talks only to the PC.
- CoinGecko without a key answers 429 quickly: after one, the deck pauses it for 20 min (market caps
  and the global market keep their last values), like the PC app.
- Developer aid: USB console `nopc 1|0` ignores the paired PC until reboot.
