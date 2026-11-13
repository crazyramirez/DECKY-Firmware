#include "demo_source.h"

#include <Arduino.h>
#include <string.h>

#include "app/event_bus.h"
#include "commands/command_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {
namespace demo {
namespace {

const char *TAG = "demo";
TaskHandle_t s_task = nullptr;
volatile bool s_stop = false;
volatile bool s_paused = false;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float noise(float amp) { return (static_cast<float>(esp_random() % 2001) / 1000.0f - 1.0f) * amp; }

struct Sim {
  float cpu_load = 18, cpu_temp = 42, gpu_load = 8, gpu_temp = 38, ram_used = 21500, nvme = 41;
  uint32_t t = 0;
};

void step(Sim &s, telemetry::Snapshot &out) {
  s.t++;
  // A 25 s "stress" phase every 120 s drives CPU into the warning/critical zone so alerts can be seen.
  const uint32_t phase = s.t % 120;
  const bool stress = phase >= 60 && phase < 85;
  const float cpu_target = stress ? 97.0f : 15.0f + 20.0f * (0.5f + 0.5f * sinf(s.t / 9.0f));
  const float gpu_target = stress ? 70.0f : 10.0f + 25.0f * (0.5f + 0.5f * sinf(s.t / 13.0f + 1.0f));
  s.cpu_load = clampf(s.cpu_load + (cpu_target - s.cpu_load) * 0.35f + noise(4), 0, 100);
  s.gpu_load = clampf(s.gpu_load + (gpu_target - s.gpu_load) * 0.25f + noise(3), 0, 100);
  // First-order thermal lag towards a load-dependent equilibrium.
  s.cpu_temp += ((34.0f + s.cpu_load * 0.64f) - s.cpu_temp) * 0.22f + noise(0.6f);
  s.gpu_temp += ((33.0f + s.gpu_load * 0.55f) - s.gpu_temp) * 0.12f + noise(0.4f);
  s.ram_used = clampf(s.ram_used + noise(120), 18000, 30000);
  s.nvme += ((40.0f + s.cpu_load * 0.08f) - s.nvme) * 0.05f;

  out = telemetry::Snapshot{};
  out.seq = s.t;
  out.rx_ms = millis();
  out.synthetic = true;
  strlcpy(out.host, "DEMO-PC", sizeof(out.host));

  strlcpy(out.cpu.name, "CPU sintética (demo)", sizeof(out.cpu.name));
  out.cpu.load_pct = s.cpu_load;
  out.cpu.temp_c = s.cpu_temp;
  out.cpu.core_max_temp_c = s.cpu_temp + 4.0f + noise(1.0f);
  out.cpu.freq_mhz = 3200.0f + s.cpu_load * 23.0f;
  out.cpu.package_power_w = 18.0f + s.cpu_load * 2.1f + noise(2);

  strlcpy(out.gpu.name, "GPU sintética (demo)", sizeof(out.gpu.name));
  out.gpu.load_pct = s.gpu_load;
  out.gpu.temp_c = s.gpu_temp;
  out.gpu.vram_used_mib = 1800.0f + s.gpu_load * 60.0f;
  out.gpu.vram_total_mib = 12288.0f;
  out.gpu.board_power_w = 22.0f + s.gpu_load * 2.6f;

  out.ram.used_mib = s.ram_used;
  out.ram.total_mib = 65536.0f;

  out.storage_count = 1;
  strlcpy(out.storage[0].name, "NVMe demo", sizeof(out.storage[0].name));
  out.storage[0].temp_c = s.nvme;

  // Two fans with values and one deliberately missing reading (null), as on real boards.
  out.fan_count = 3;
  strlcpy(out.fans[0].label, "CPU Fan", sizeof(out.fans[0].label));
  out.fans[0].rpm = 700.0f + s.cpu_load * 9.0f;
  strlcpy(out.fans[1].label, "Chassis 1", sizeof(out.fans[1].label));
  out.fans[1].rpm = 600.0f + s.cpu_load * 4.0f;
  strlcpy(out.fans[2].label, "Chassis 2", sizeof(out.fans[2].label));
  out.fans[2].rpm = telemetry::kMissing;
}

void answer_commands() {
  commands::Request r;
  while (commands::next_outbound(r, 0)) {
    vTaskDelay(pdMS_TO_TICKS(120));  // simulated LAN round trip
    Event e;
    e.type = EventType::CommandResult;
    e.cmd = CommandResult{};
    e.cmd.request_id = r.request_id;
    e.cmd.simulated = true;
    strlcpy(e.cmd.action, commands::action_name(r.action), sizeof(e.cmd.action));
    e.cmd.status = CmdStatus::Ack;
    e.cmd.applied_value = r.value;
    bus::post(e, 50);
  }
}

void task(void *) {
  Sim sim;
  telemetry::Snapshot snap;
  uint32_t next = millis();
  ESP_LOGI(TAG, "demo source started (synthetic data)");
  while (!s_stop) {
    answer_commands();
    if (static_cast<int32_t>(millis() - next) >= 0) {
      next += 1000;
      step(sim, snap);
      if (!s_paused) {
        Event e;
        e.type = EventType::Telemetry;
        e.snapshot = snap;
        bus::post(e, 10);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  ESP_LOGI(TAG, "demo source stopped");
  s_task = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

bool start() {
  // A previous stop() may still be winding down: wait (bounded) for the task to exit.
  for (int i = 0; i < 20 && s_task && s_stop; i++) vTaskDelay(pdMS_TO_TICKS(10));
  if (s_task) {
    s_stop = false;  // still alive: cancel the pending stop instead of spawning a second task
    return true;
  }
  s_stop = false;
  // Core 0, below the Arduino loop priority; 4 KB stack is plenty (no recursion, snapshot on stack ~0.4 KB).
  return xTaskCreatePinnedToCore(task, "demo_src", 4096, nullptr, 2, &s_task, 0) == pdPASS;
}

void stop() { s_stop = true; }
bool running() { return s_task != nullptr; }
void set_paused(bool p) { s_paused = p; }
bool paused() { return s_paused; }

}  // namespace demo
}  // namespace app
