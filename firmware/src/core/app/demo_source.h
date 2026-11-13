#pragma once
// DEMO MODE ONLY. Generates clearly-labelled synthetic telemetry (Snapshot.synthetic = true)
// at 1 Hz from its own FreeRTOS task and answers commands with simulated ACKs, exercising the
// same event-bus path the network client will use. Never active unless demo mode is enabled.

namespace app {
namespace demo {

bool start();
void stop();
bool running();
void set_paused(bool paused);  // stops emitting snapshots, to exercise STALE / OFFLINE
bool paused();

}  // namespace demo
}  // namespace app
