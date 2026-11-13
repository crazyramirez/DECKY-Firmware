#pragma once
// Application controller: owns the UI task loop (Arduino loop), drains the event bus,
// evaluates link freshness (ONLINE -> STALE -> OFFLINE) and applies user settings.

#include <stdint.h>

#include "storage/settings_store.h"

#include "app/version.h"

namespace app {

void setup();
void loop();

// Called from the UI (same task).
void set_brightness(uint8_t percent, bool persist);
void set_language(storage::Lang lang);  // deferred UI rebuild
void set_alerts_enabled(bool on);
void set_refresh(uint8_t seconds);  // 1, 2 or 5; persisted
void set_auto_sleep(uint8_t minutes);  // 0 (never), 1, 2, 5, 10; persisted
void set_screensaver(const storage::Screensaver &ss);  // validated, persisted
void set_tab_order(const uint8_t order[storage::kTabCount]);  // persisted, nav reordered at once
void set_tab_hidden(uint8_t tab, bool hidden);  // persisted, nav updated at once
void set_place(const char *name, float lat, float lon);  // weather place; persisted, refetches
void set_temp_f(bool fahrenheit);
void set_clock_12h(bool twelve);
void set_today_bg(bool on);  // Today: weather picture behind the page
void set_button_action(uint8_t action);  // sleep button (PCD_HAS_SLEEP_BUTTON): 0 deep sleep, 1 screen off/on
// Crypto tab (all persisted).
void set_crypto_coins(const char *csv);  // "BTC,ETH,..." (catalog symbols, <= 12)
void set_crypto_currency(uint8_t cur);   // storage::Currency
void set_crypto_refresh(uint8_t seconds);  // 15, 30 or 60
void set_crypto_style(uint8_t style);    // 0 line, 1 candles
void set_crypto_range(uint8_t range);    // last chart range used
// Tells the network task what to fetch: watched coins, currency, and how often (faster while a
// Crypto page is visible, slow in the background, off when the tab is hidden).
void crypto_sync();
void set_demo_mode(bool on);
void set_demo_paused(bool paused);

// Reset reason of the current boot (esp_reset_reason) as text, e.g. "PANIC", "BROWNOUT".
const char *reset_reason();

}  // namespace app
