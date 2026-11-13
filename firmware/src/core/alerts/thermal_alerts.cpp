#include "thermal_alerts.h"

#include <math.h>

namespace alerts {

Level ThermalAlert::raw_level(float temp_c) const {
  // Going up uses the plain thresholds; going down requires dropping below threshold - hysteresis.
  switch (level_) {
    case Level::Critical:
      if (temp_c >= t_.critical_c - t_.hysteresis_c) return Level::Critical;
      if (temp_c >= t_.warning_c - t_.hysteresis_c) return Level::Warning;
      return Level::Normal;
    case Level::Warning:
      if (temp_c >= t_.critical_c) return Level::Critical;
      if (temp_c >= t_.warning_c - t_.hysteresis_c) return Level::Warning;
      return Level::Normal;
    case Level::Normal:
    default:
      if (temp_c >= t_.critical_c) return Level::Critical;
      if (temp_c >= t_.warning_c) return Level::Warning;
      return Level::Normal;
  }
}

bool ThermalAlert::update(float temp_c, uint32_t now_ms) {
  if (isnan(temp_c) || isinf(temp_c)) return false;

  const Level candidate = raw_level(temp_c);
  if (candidate == level_) {
    pending_active_ = false;
    return false;
  }

  // Raising the level needs the condition to hold for min_duration; lowering is immediate
  // (hysteresis already filters noise on the way down).
  if (candidate > level_) {
    if (!pending_active_ || pending_ != candidate) {
      pending_ = candidate;
      pending_since_ = now_ms;
      pending_active_ = true;
      if (t_.min_duration_ms > 0) return false;
    }
    if (now_ms - pending_since_ < t_.min_duration_ms) return false;
  }

  pending_active_ = false;
  const Level previous = level_;
  level_ = candidate;
  if (level_ <= previous) return false;  // de-escalation: no notification

  const bool escalated_past_last = !notified_once_ || level_ > last_notified_level_;
  const bool cooldown_over = !notified_once_ || (now_ms - last_notify_ms_ >= t_.cooldown_ms);
  if (escalated_past_last || cooldown_over) {
    notified_once_ = true;
    last_notify_ms_ = now_ms;
    last_notified_level_ = level_;
    return true;
  }
  return false;
}

void ThermalAlert::reset() {
  level_ = Level::Normal;
  pending_active_ = false;
  notified_once_ = false;
  last_notified_level_ = Level::Normal;
}

}  // namespace alerts
