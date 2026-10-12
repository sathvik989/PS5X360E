// SPDX-License-Identifier: MIT
// Pure selection state for an estimated-hand NUI message box. No controller
// buttons, hidden clock, profile/pose access, guest writes or SDK claims.
#pragma once
#include <cstdint>

namespace xbox360ps5::motion {
class MotionMenu {
 public:
  uint32_t selected;
  float progress = 0;

  explicit MotionMenu(uint32_t initial_button = 0) : selected(initial_button) {}

  // Caller supplies right-hand direction and confidence/profile/freshness gates.
  // 0=neutral, 1=previous, 2=next, 4=confirm. Only confirmation returns true.
  // A valid neutral sample arms the next continuous 250ms hold; completing any
  // direction consumes that arm, even if a lateral move reaches a boundary.
  bool Update(bool valid, uint32_t session, int direction, int64_t now_ms,
              uint32_t button_count) {
    selected = button_count ? (selected < button_count ? selected : button_count - 1) : 0;
    if (!seen_session_ || session != session_) {
      Reset();
      session_ = session;
      seen_session_ = true;
      seen_time_ = false;
    }
    if (seen_time_ && now_ms < last_ms_) {
      Reset();
      last_ms_ = now_ms;
      return false;  // A later valid neutral must rearm after clock rollback.
    }
    last_ms_ = now_ms;
    seen_time_ = true;
    if (button_count != count_) {
      Reset();  // A hold cannot carry over to a changed set of buttons.
      count_ = button_count;
    }
    if (!valid || !button_count ||
        (direction != 0 && direction != 1 && direction != 2 && direction != 4)) {
      Reset();
      return false;
    }
    if (direction == 0) {
      Reset();
      armed_ = true;
      return false;
    }
    if (!armed_) return false;
    if (direction != pending_) {
      pending_ = direction;
      started_ms_ = now_ms;
      progress = 0;
      return false;
    }
    // Unsigned subtraction avoids signed overflow across INT64_MIN/MAX. The
    // rollback check guarantees chronological order within the current hold.
    const uint64_t elapsed = uint64_t(now_ms) - uint64_t(started_ms_);
    progress = elapsed >= 250 ? 1.0f : float(elapsed) / 250.0f;
    if (elapsed < 250) return false;
    armed_ = false;
    pending_ = 0;
    if (direction == 1 && selected > 0) --selected;
    if (direction == 2 && selected < button_count - 1) ++selected;
    return direction == 4;
  }

 private:
  void Reset() {
    progress = 0;
    armed_ = false;
    pending_ = 0;
  }
  bool armed_ = false, seen_session_ = false, seen_time_ = false;
  uint32_t session_ = 0, count_ = 0;
  int pending_ = 0;
  int64_t started_ms_ = 0, last_ms_ = 0;
};
}  // namespace xbox360ps5::motion
