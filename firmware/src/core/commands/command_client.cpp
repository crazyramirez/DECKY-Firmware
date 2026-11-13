#include "command_client.h"

#include <Arduino.h>
#include <string.h>

#include "app/event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace commands {
namespace {

const char *TAG = "cmd";
constexpr int kMaxPending = 4;

struct Pending {
  uint32_t id = 0;
  Action action = Action::VolumeSet;
  uint32_t deadline_ms = 0;
};

QueueHandle_t s_out = nullptr;
Pending s_pending[kMaxPending];
uint32_t s_next_id = 1;

bool valid(Action a, int32_t value) {
  if (a == Action::VolumeSet) return value >= 0 && value <= 100;
  if (a == Action::MediaSeek) return value >= 0 && value <= 86400;
  return true;
}

}  // namespace

bool init() {
  if (!s_out) s_out = xQueueCreate(4, sizeof(Request));
  return s_out != nullptr;
}

const char *action_name(Action a) {
  switch (a) {
    case Action::VolumeSet: return "volume.set";
    case Action::MediaPlayPause: return "media.play_pause";
    case Action::MediaNext: return "media.next";
    case Action::MediaPrevious: return "media.previous";
    case Action::SessionLock: return "session.lock";
    case Action::SystemShutdown: return "system.shutdown";
    case Action::SystemRestart: return "system.restart";
    case Action::MediaSeek: return "media.seek";
  }
  return "?";
}

uint32_t send(Action a, int32_t value) {
  if (!s_out || !valid(a, value)) return 0;
  Pending *slot = nullptr;
  for (auto &p : s_pending) {
    if (p.id == 0) {
      slot = &p;
      break;
    }
  }
  if (!slot) {
    ESP_LOGW(TAG, "too many pending commands, dropping %s", action_name(a));
    return 0;
  }
  Request r{s_next_id++, a, (a == Action::VolumeSet || a == Action::MediaSeek) ? value : -1};
  if (s_next_id == 0) s_next_id = 1;
  if (xQueueSend(s_out, &r, 0) != pdTRUE) return 0;
  slot->id = r.request_id;
  slot->action = a;
  slot->deadline_ms = millis() + kTimeoutMs;
  ESP_LOGI(TAG, "-> #%u %s %d", (unsigned)r.request_id, action_name(a), (int)r.value);
  return r.request_id;
}

bool next_outbound(Request &out, uint32_t wait_ms) {
  return s_out && xQueueReceive(s_out, &out, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

bool resolve(uint32_t request_id) {
  for (auto &p : s_pending) {
    if (p.id != 0 && p.id == request_id) {
      p.id = 0;
      return true;
    }
  }
  return false;  // unknown, duplicate or arrived after timeout
}

void check_timeouts(uint32_t now_ms) {
  for (auto &p : s_pending) {
    if (p.id != 0 && static_cast<int32_t>(now_ms - p.deadline_ms) >= 0) {
      app::Event e;
      e.type = app::EventType::CommandResult;
      e.cmd = app::CommandResult{};
      e.cmd.request_id = p.id;
      e.cmd.status = app::CmdStatus::Timeout;
      strlcpy(e.cmd.action, action_name(p.action), sizeof(e.cmd.action));
      ESP_LOGW(TAG, "<- #%u %s TIMEOUT", (unsigned)p.id, e.cmd.action);
      p.id = 0;
      app::bus::post(e);
    }
  }
}

int pending_count() {
  int n = 0;
  for (auto &p : s_pending) n += p.id != 0;
  return n;
}

}  // namespace commands
