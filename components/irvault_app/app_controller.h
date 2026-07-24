#pragma once

#include "button_manager.h"
#include "ir_manager.h"
#include "screen_manager.h"
#include "sticks3_hardware.h"
#include "storage_manager.h"
#include "esphome/core/component.h"

namespace esphome::irvault {

/** Owns the application state and is the only state transition authority. */
class AppController : public Component {
 public:
  void setup() override;
 void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  bool request_send_slot(uint8_t slot);

 protected:
  enum class AppState : uint8_t {
    HOME,
    ACTION_MENU,
    NOTICE,
    LEARNING,
    SAVE_CONFIRM,
    SENDING,
    RESULT,
  };

  void handle_event_(ButtonEvent event);
  void refresh_screen_();
  void enter_state_(AppState state, uint32_t now_ms);
  void start_learning_(uint32_t now_ms);
  bool send_selected_(uint32_t now_ms);

  StickS3Hardware hardware_;
  ButtonManager buttons_;
  ScreenManager screen_;
  IRManager ir_;
  StorageManager storage_;
  IRProfile *pending_profile_{nullptr};
  AppState state_{AppState::HOME};
  uint8_t selected_slot_{0};
  uint8_t selected_action_{0};
  uint8_t confirm_choice_{0};
  const char *result_title_{"Done"};
  const char *result_detail_{""};
  bool result_auto_return_{false};
  uint32_t state_entered_at_{0};
  uint32_t last_status_refresh_{0};
  uint32_t last_battery_refresh_{0};
};

}  // namespace esphome::irvault
