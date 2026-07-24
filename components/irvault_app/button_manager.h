#pragma once

#include <Arduino.h>
#include <cstdint>

namespace esphome::irvault {

enum class ButtonEvent : uint8_t {
  NONE,
  SHORT_A,
  SHORT_B,
  LONG_A,
  LONG_B,
  BOTH_LONG,
};

/** Debounces the two active-low StickS3 buttons without blocking. */
class ButtonManager {
 public:
  void setup();
  ButtonEvent update(uint32_t now_ms);

 protected:
  struct ButtonState {
    bool raw_pressed{false};
    bool stable_pressed{false};
    uint32_t raw_changed_at{0};
    uint32_t pressed_at{0};
  };

  bool update_button_(ButtonState &state, bool pressed, uint32_t now_ms);

  ButtonState a_;
  ButtonState b_;
  bool chord_active_{false};
  bool chord_emitted_{false};
  bool suppress_a_release_{false};
  bool suppress_b_release_{false};
  uint32_t chord_started_at_{0};
};

}  // namespace esphome::irvault
