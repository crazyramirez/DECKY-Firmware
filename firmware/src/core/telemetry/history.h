#pragma once
// Fixed-size ring buffer of samples (no heap). NaN marks a missing sample so gaps stay visible.

#include <stdint.h>

#include "models.h"

namespace telemetry {

template <int N>
class History {
 public:
  static constexpr int kCapacity = N;

  void clear() {
    head_ = 0;
    count_ = 0;
  }

  void push(float v) {
    buf_[head_] = v;
    head_ = (head_ + 1) % N;
    if (count_ < N) count_++;
  }

  int size() const { return count_; }

  // i = 0 is the oldest sample still stored, size()-1 the newest.
  float at(int i) const {
    if (i < 0 || i >= count_) return kMissing;
    const int start = (head_ - count_ + N) % N;
    return buf_[(start + i) % N];
  }

  float latest() const { return count_ ? at(count_ - 1) : kMissing; }

  // Max over stored samples, ignoring missing ones. NaN if none.
  float max() const {
    float m = kMissing;
    for (int i = 0; i < count_; i++) {
      const float v = at(i);
      if (has(v) && (!has(m) || v > m)) m = v;
    }
    return m;
  }

 private:
  float buf_[N] = {};
  int head_ = 0;
  int count_ = 0;
};

constexpr int kHistorySamples = 60;  // 60 s at 1 Hz
using History60 = History<kHistorySamples>;

}  // namespace telemetry
