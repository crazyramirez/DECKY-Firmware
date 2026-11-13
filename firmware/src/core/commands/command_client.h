#pragma once
// Command requests from the UI with requestId correlation, ACK/NACK and timeout.
// The UI never reports success before an ACK arrives (spec §7).
//
// Transport is pluggable: in Hito 1 the demo source consumes the outbound queue and answers
// with simulated ACKs; in Hito 4 the WebSocket client will consume the same queue.

#include <stdint.h>

namespace commands {

enum class Action : uint8_t { VolumeSet, MediaPlayPause, MediaNext, MediaPrevious, SessionLock, SystemShutdown, SystemRestart, MediaSeek };

struct Request {
  uint32_t request_id;
  Action action;
  int32_t value;  // volume 0..100, seek position in seconds, otherwise -1
};

constexpr uint32_t kTimeoutMs = 3000;

bool init();
const char *action_name(Action a);  // protocol name, e.g. "volume.set"

// UI task: enqueue a command. Returns request id, or 0 if rejected locally (invalid / queue full).
uint32_t send(Action a, int32_t value = -1);

// Transport side: pop the next outbound request (any task).
bool next_outbound(Request &out, uint32_t wait_ms);

// UI task: correlate a result. Returns false for unknown / duplicate / late request ids.
bool resolve(uint32_t request_id);

// UI task: emits Timeout results (through the event bus) for expired pending requests.
void check_timeouts(uint32_t now_ms);

int pending_count();

}  // namespace commands
