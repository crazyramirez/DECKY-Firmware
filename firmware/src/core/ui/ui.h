#pragma once
// Public UI API. All functions must be called from the UI task (Arduino loop).

#include <stdint.h>

#include "app/event_bus.h"

namespace ui {

void build();                 // (re)creates the whole UI, e.g. after a language change
void apply_tab_order();       // bottom bar follows app::state().settings.tab_order
void on_telemetry();          // new snapshot ingested into app::state()
void on_link();               // link state or capabilities changed
void on_command_result(const app::CommandResult &r);
void on_alert(uint8_t mask);  // bit0 CPU, bit1 GPU
void tick_1s();               // diagnostics refresh
void on_wifi_scan();          // Wi-Fi scan results ready (net::scan_results)
void on_discovery();          // mDNS results ready (net::discovery_results)
void on_pair_done(const app::PairOutcome &r);
void on_art(const app::ArtFrame &art);  // takes ownership of art.data
void on_weather();            // app::state().weather / weather_error / units changed
void on_places();             // city search results ready (net::place_results)
void on_crypto();             // crypto prices arrived / failed (crypto_store)
void on_crypto_chart();       // a crypto chart arrived / failed (crypto_store)
void on_pc_presence();       // app::state().pc_present changed: PC tabs shown / hidden
void on_crypto_watch();       // watched coins / currency changed (settings)
// Screensaver (wallpaper + clock + chosen data) on the top layer; a touch or BOOT closes it.
namespace screensaver {
void start();   // idle timeout (app) or Settings preview
void stop();    // no-op when not showing
bool active();
}  // namespace screensaver

// Short non-blocking notification at the bottom of the content area, above a bottom tab bar
// (errors, confirmations of system actions).
void toast(const char *text, uint32_t color);
// Modal confirmation; on_ok runs in the UI task only if the user confirms.
void confirm(const char *text, const char *ok_label, uint32_t ok_color, void (*on_ok)(void *), void *user);

}  // namespace ui
