#pragma once
// Thermal alert evaluator. Pure logic (no Arduino/LVGL) so it can be unit-tested.
//
// Defaults are illustrative, NOT universal limits (spec §6): CPU 85/95 °C, GPU 80/90 °C.
// Hysteresis, minimum time above threshold and a re-notify cooldown avoid alert spam.
// A critical alert is only a notification: V1 never shuts anything down.

#include <stdint.h>

namespace alerts {

enum class Level : uint8_t { Normal = 0, Warning = 1, Critical = 2 };

struct Thresholds {
  float warning_c;
  float critical_c;
  float hysteresis_c = 3.0f;
  uint32_t min_duration_ms = 3000;
  uint32_t cooldown_ms = 60000;
};

class ThermalAlert {
 public:
  explicit ThermalAlert(const Thresholds &t) : t_(t) {}

  void set_thresholds(const Thresholds &t) { t_ = t; }
  const Thresholds &thresholds() const { return t_; }

  // Feed one sample. Missing samples (NaN) never raise or clear an alert.
  // Returns true when the caller should notify the user (new or escalated level after cooldown).
  bool update(float temp_c, uint32_t now_ms);

  Level level() const { return level_; }
  void reset();

 private:
  Level raw_level(float temp_c) const;

  Thresholds t_;
  Level level_ = Level::Normal;
  Level pending_ = Level::Normal;
  uint32_t pending_since_ = 0;
  bool pending_active_ = false;
  uint32_t last_notify_ms_ = 0;
  bool notified_once_ = false;
  Level last_notified_level_ = Level::Normal;
};

}  // namespace alerts
