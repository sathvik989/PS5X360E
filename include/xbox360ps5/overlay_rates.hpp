// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cmath>

namespace ps5x360::overlay {
// Accepted guest-picture presents, not vblank events or physical scanout.
// This main-local state never changes presenter/driver/header layouts.
class Rates {
 public:
  using Clock = std::chrono::steady_clock;
  void Update(Clock::time_point now, uint64_t accepted, uint64_t generated,
              bool enabled, int fallback) {
    fallback_ = fallback;
    if (!initialized_ || now < started_ || accepted < accepted_ ||
        generated < generated_ || enabled != enabled_) {
      Reset(now, accepted, generated, enabled);
      return;
    }
    const double seconds = std::chrono::duration<double>(now - started_).count();
    if (!std::isfinite(seconds) || seconds < 0) {
      Reset(now, accepted, generated, enabled);
      return;
    }
    if (seconds < .5) return; // Includes elapsed=0; never divide by zero.
    const uint64_t output_delta = accepted - accepted_;
    // Atomic counters are read independently. Do not display generated output
    // above total output due to a snapshot straddling a presenter increment.
    const uint64_t fg_delta = std::min(generated - generated_, output_delta);
    output_fps_ = double(output_delta) / seconds;
    fg_fps_ = enabled ? double(fg_delta) / seconds : 0;
    started_ = now; accepted_ = accepted; generated_ = generated;
  }
  double output_fps() const { return output_fps_; }
  double generated_fps() const { return fg_fps_; }
  bool waiting() const { return enabled_ && fg_fps_ == 0; }
  const char* status() const { return !enabled_ ? "FG Off" : waiting() ? "FG waiting" : "FG active"; }
  int fallback() const { return fallback_; }
  void Label(char* text, size_t capacity) const {
    std::snprintf(text, capacity, "Output (present) %.0f FPS | FG %.0f FPS | %s",
                  output_fps_, fg_fps_, status());
  }
 private:
  void Reset(Clock::time_point now, uint64_t accepted, uint64_t generated, bool enabled) {
    initialized_ = true; started_ = now; accepted_ = accepted;
    generated_ = generated; enabled_ = enabled; output_fps_ = fg_fps_ = 0;
  }
  bool initialized_ = false, enabled_ = false;
  Clock::time_point started_{};
  uint64_t accepted_ = 0, generated_ = 0;
  double output_fps_ = 0, fg_fps_ = 0;
  int fallback_ = 0;
};
}  // namespace ps5x360::overlay
