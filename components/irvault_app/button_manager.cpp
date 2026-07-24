#include "button_manager.h"

namespace esphome::irvault {

static constexpr uint8_t PIN_A = 11;
static constexpr uint8_t PIN_B = 12;
static constexpr uint32_t DEBOUNCE_MS = 30;
static constexpr uint32_t LONG_PRESS_MS = 1200;

void ButtonManager::setup() {
  pinMode(PIN_A, INPUT);
  pinMode(PIN_B, INPUT);
}

bool ButtonManager::update_button_(ButtonState &state, bool pressed, uint32_t now_ms) {
  if (pressed != state.raw_pressed) {
    state.raw_pressed = pressed;
    state.raw_changed_at = now_ms;
  }
  if (state.stable_pressed != state.raw_pressed &&
      now_ms - state.raw_changed_at >= DEBOUNCE_MS) {
    state.stable_pressed = state.raw_pressed;
    if (state.stable_pressed)
      state.pressed_at = now_ms;
    return true;
  }
  return false;
}

ButtonEvent ButtonManager::update(uint32_t now_ms) {
  const bool a_changed = this->update_button_(this->a_, digitalRead(PIN_A) == LOW, now_ms);
  const bool b_changed = this->update_button_(this->b_, digitalRead(PIN_B) == LOW, now_ms);

  if (this->a_.stable_pressed && this->b_.stable_pressed) {
    if (!this->chord_active_) {
      this->chord_active_ = true;
      this->chord_emitted_ = false;
      this->chord_started_at_ = now_ms;
      this->suppress_a_release_ = true;
      this->suppress_b_release_ = true;
    }
    if (!this->chord_emitted_ && now_ms - this->chord_started_at_ >= LONG_PRESS_MS) {
      this->chord_emitted_ = true;
      return ButtonEvent::BOTH_LONG;
    }
    return ButtonEvent::NONE;
  }

  if (this->chord_active_ && !this->a_.stable_pressed && !this->b_.stable_pressed)
    this->chord_active_ = false;

  if (a_changed && !this->a_.stable_pressed) {
    if (this->suppress_a_release_) {
      this->suppress_a_release_ = false;
    } else {
      return now_ms - this->a_.pressed_at >= LONG_PRESS_MS ? ButtonEvent::LONG_A
                                                           : ButtonEvent::SHORT_A;
    }
  }
  if (b_changed && !this->b_.stable_pressed) {
    if (this->suppress_b_release_) {
      this->suppress_b_release_ = false;
    } else {
      return now_ms - this->b_.pressed_at >= LONG_PRESS_MS ? ButtonEvent::LONG_B
                                                           : ButtonEvent::SHORT_B;
    }
  }
  return ButtonEvent::NONE;
}

}  // namespace esphome::irvault
