#pragma once
// Crypto data shared between the network task (writer) and the UI task (reader). Every accessor
// takes the store mutex; the buffers live in PSRAM. The UI is told about new data through the
// event bus (EventType::CryptoData / CryptoChart) and then copies what it needs.

#include <stdint.h>

#include "crypto/models.h"

namespace crypto {

bool store_init();

// Network task.
void store_market(const Market &m);   // ok or error (an error keeps the last good prices)
void store_chart(const Chart &c);     // ok or error, cached per (pair, range, currency)

// UI task. get_market: false until the first answer; `rx_ms` = millis() of the last good data.
bool get_market(Market &out, uint32_t *rx_ms);
void market_error(char *out, unsigned n);  // last error ("" = none)
// Cached chart for (pair, range, cur): true when found (ok or error); age_ms since it arrived.
bool get_chart(const char *pair, uint8_t range, const char *cur, Chart &out, uint32_t *age_ms);
void clear_charts();                  // currency changed

}  // namespace crypto
