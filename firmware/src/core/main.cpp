// DECKY firmware entry point. The PlatformIO environment selects the app:
//   env:hwtest -> Hito 0 hardware validation
//   env:deck   -> product UI
#include <Arduino.h>

#if defined(PCD_APP_HWTEST)
#include "hwtest/hw_test.h"
void setup() { hwtest::setup(); }
void loop() { hwtest::loop(); }
#elif defined(PCD_APP_DECK)
#include "app/app_controller.h"
void setup() { app::setup(); }
void loop() { app::loop(); }
#else
#error "Build with -DPCD_APP_HWTEST=1 or -DPCD_APP_DECK=1 (use a PlatformIO env)"
#endif
