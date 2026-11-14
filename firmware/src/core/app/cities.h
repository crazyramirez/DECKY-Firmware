#pragma once
// Today's cities: the main place (settings.place_*, index 0: it also sets the deck's clock without a
// PC and feeds the screensaver) plus up to storage::kMaxMoreCities more, each with its own forecast and
// local time. Today shows one of them at a time (today_city()); a swipe or the Cities sheet changes it.
// UI task only. Every change is persisted, tells the network task what to fetch and calls
// ui::on_weather().

#include <stdint.h>
#include <time.h>

#include "storage/settings_store.h"
#include "telemetry/models.h"

namespace app {

int city_count();  // 0 until a main place is chosen
const char *city_name(int i);
const telemetry::Weather &city_weather(int i);  // .ok == false: nothing yet
const char *city_error(int i);                  // last failure ("" = none)
// Local time there. False until the clock is set or, for a city just added, its UTC offset is known.
bool city_time(int i, struct tm &out);
// Minutes that city is ahead of the deck's clock (negative: behind). False until known.
bool city_offset(int i, int &minutes);
// Its date against the deck's: -1 yesterday, 0 today, +1 tomorrow (0 while unknown).
int city_day_shift(int i);

int today_city();          // the city Today shows; 0 = the main place
void show_city(int i);     // clamped
bool step_city(int dir);   // next (+1) / previous (-1); false at either end

// index of the city (the existing one when it was already there), -1 when the list is full.
// Without a main place it becomes the main place.
int add_city(const char *name, float lat, float lon);
void remove_city(int i);     // removing the main place promotes the next city
void make_main_city(int i);  // swaps it with the main place

void sync_weather_places();  // hand the list to the network task (start-up)
// A forecast (or a failure) from the network task, filed under its place. Returns the city index it
// went to (the first one), -1 when no city matches any more.
int ingest_weather(const telemetry::Weather &w, uint32_t now_ms);

}  // namespace app
