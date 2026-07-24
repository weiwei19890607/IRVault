#pragma once

#include <cstdint>
#include "ir_profile.h"

namespace esphome::irvault {

/** Renders the IRVault portrait UI through M5GFX. */
class ScreenManager {
 public:
  void setup();
  void set_battery_level(int level);
  void render_home(uint8_t selected_slot, const bool learned[3], bool wifi_connected,
                   bool api_connected);
  void render_action_menu(uint8_t selected_slot, uint8_t selected_action);
  void render_notice(uint8_t selected_slot, uint8_t selected_action);
  void render_learning(uint8_t selected_slot, uint8_t seconds_left,
                       uint16_t invalid_frames, uint16_t noise_events,
                       uint8_t matched_frames, const char *protocol);
  void render_save_confirm(uint8_t selected_slot, const IRProfile &profile, uint8_t choice);
  void render_sending(uint8_t selected_slot, uint16_t pulse_count, uint32_t carrier_hz);
  void render_result(const char *title, const char *detail);

 protected:
  enum class View : uint8_t {
    NONE,
    HOME,
    ACTION_MENU,
    NOTICE,
    LEARNING,
    SAVE_CONFIRM,
    SENDING,
    RESULT
  };

  View view_{View::NONE};
  uint8_t selected_slot_{255};
  uint8_t selected_action_{255};
  uint8_t learning_seconds_left_{255};
  uint16_t learning_invalid_frames_{UINT16_MAX};
  uint16_t learning_noise_events_{UINT16_MAX};
  uint8_t learning_matched_frames_{255};
  const char *learning_protocol_{nullptr};
  bool wifi_connected_{false};
  bool api_connected_{false};
  bool learned_[3]{false, false, false};
  int battery_level_{-1};

  void begin_page_(const char *title);
  void draw_footer_(const char *left, const char *right);
  void draw_choice_(int y, const char *label, bool selected);
  void draw_battery_();
};

}  // namespace esphome::irvault
