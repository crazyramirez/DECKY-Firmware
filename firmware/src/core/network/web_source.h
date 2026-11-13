#pragma once
// Direct internet data, for when no PC app is connected: the deck asks the same public services the
// PC app uses (Open-Meteo for the weather and the city search; Binance, CoinGecko and alternative.me
// for crypto) over HTTPS, verified with the framework's root certificate bundle. No API keys.
//
// Every call blocks (one request, usually < 1 s): network task only. One TLS connection at a time
// (mbedTLS keeps its buffers in internal RAM); it stays open while the next request goes to the same
// host, so a price refresh plus its sparklines reuse one Binance connection.

#include <stdint.h>

#include "crypto/models.h"
#include "telemetry/models.h"

namespace net {
namespace web {

// Forecast for (lat, lon): out.ok, or out.error ("unavailable", "bad_response"). out.tz_min = the
// place's UTC offset (clock without a PC).
bool forecast(float lat, float lon, telemetry::Weather &out);

// City search (Open-Meteo geocoding): number of places (0 = none), -1 = the service failed.
int search(const char *query, const char *lang, telemetry::Place *out, int max);

// Crypto. coins = catalog indices (crypto/coin_catalog.h), cur = display currency code.
// prices(): 24 h tickers (+ the currency rate); false when Binance could not be reached.
// enrich(): one pending slower refresh (a sparkline, market caps, global market, fear & greed);
//           false when nothing was due. build(): the market from what is cached, in `cur`.
bool prices(const int8_t *coins, int n, const char *cur);
bool enrich(const int8_t *coins, int n);
void build(const int8_t *coins, int n, const char *cur, crypto::Market &out);

// Chart candles for (pair, range) in `cur`: out.ok or out.error.
void chart(const char *pair, uint8_t range, const char *cur, crypto::Chart &out);

// Close the kept-alive connection when it has been idle for a while (or at once: force).
void idle(bool force);

// A request answered within the last `ms`: the internet is reachable over this Wi-Fi.
bool answered_within(uint32_t ms);

}  // namespace web
}  // namespace net
