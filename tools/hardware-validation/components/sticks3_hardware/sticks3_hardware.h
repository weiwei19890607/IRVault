#pragma once

#include "esphome/core/component.h"

namespace esphome::sticks3_hardware {

enum TestMode : uint8_t { TEST_A, TEST_B, TEST_C, TEST_D };
enum ButtonPull : uint8_t { PULL_NONE, PULL_UP, PULL_DOWN };

/** Phase 0-only adapter used to probe StickS3 hardware feasibility. */
class StickS3Hardware : public Component {
 public:
  void set_test_mode(TestMode mode) { this->test_mode_ = mode; }
  void set_button_pull(ButtonPull pull) { this->button_pull_ = pull; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

 protected:
  bool initialize_m5_();
  bool initialize_pm1_();
  bool disable_speaker_amplifier_();
  void log_memory_info_();
  void setup_display_test_();
  void setup_button_test_();
  void poll_buttons_();

  TestMode test_mode_{TestMode::TEST_A};
  ButtonPull button_pull_{ButtonPull::PULL_NONE};
  bool m5_initialized_{false};
  bool pm1_initialized_{false};
  int last_key1_{-1};
  int last_key2_{-1};
};

}  // namespace esphome::sticks3_hardware
