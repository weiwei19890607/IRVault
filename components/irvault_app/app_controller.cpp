#include "app_controller.h"

#include <Arduino.h>

#include "esphome/components/api/api_server.h"
#include "esphome/components/network/util.h"
#include "esphome/core/log.h"

namespace esphome::irvault {

static const char *const TAG = "irvault.app";
static constexpr uint32_t LEARNING_TIMEOUT_MS = 30000;
static constexpr uint32_t SENDING_TIMEOUT_MS = 3000;
static constexpr uint32_t SENT_AUTO_RETURN_MS = 1500;

float AppController::get_setup_priority() const { return setup_priority::BUS; }

void AppController::setup() {
  if (!this->hardware_.setup()) {
    ESP_LOGE(TAG, "[APP] StickS3 hardware initialization failed");
    this->mark_failed();
    return;
  }
  this->buttons_.setup();
  this->screen_.setup();
  this->screen_.set_battery_level(this->hardware_.battery_level());
  this->pending_profile_ = allocate_ir_profile();
  if (this->pending_profile_ == nullptr) {
    ESP_LOGE(TAG, "[APP] Could not allocate long profile in PSRAM");
    this->mark_failed();
    return;
  }
  if (!this->storage_.setup()) {
    ESP_LOGE(TAG, "[APP] Variable-length storage initialization failed");
    this->mark_failed();
    return;
  }
  if (!this->ir_.setup()) {
    ESP_LOGE(TAG, "[IR] RMT initialization failed");
    this->mark_failed();
    return;
  }
  this->state_entered_at_ = millis();
  this->refresh_screen_();
  ESP_LOGI(TAG, "[APP] Long-frame HOME ready");
}

void AppController::loop() {
  if (this->is_failed())
    return;
  const uint32_t now = millis();
  if (now - this->last_battery_refresh_ >= 10000) {
    this->last_battery_refresh_ = now;
    this->screen_.set_battery_level(this->hardware_.battery_level());
  }
  this->ir_.update();
  if (this->state_ == AppState::SENDING) {
    if (this->ir_.take_send_complete()) {
      this->result_title_ = "Sent";
      this->result_detail_ = "RAW replay complete";
      this->result_auto_return_ = true;
      this->enter_state_(AppState::RESULT, now);
    } else if (now - this->state_entered_at_ >= SENDING_TIMEOUT_MS) {
      this->ir_.cancel_send();
      this->result_title_ = "Send Timeout";
      this->result_detail_ = "RMT did not finish";
      this->result_auto_return_ = false;
      this->enter_state_(AppState::RESULT, now);
    }
  }
  if (this->state_ == AppState::LEARNING) {
    if (this->ir_.has_capture() && this->ir_.take_capture(this->pending_profile_)) {
      this->confirm_choice_ = 0;
      ESP_LOGI(TAG, "[APP] Learning capture complete pulses=%u",
               this->pending_profile_->pulse_count);
      this->enter_state_(AppState::SAVE_CONFIRM, now);
    } else if (this->ir_.has_error()) {
      this->result_title_ = "RX Error";
      this->result_detail_ = "RMT receive failed";
      this->enter_state_(AppState::RESULT, now);
    } else if (now - this->state_entered_at_ >= LEARNING_TIMEOUT_MS) {
      this->ir_.cancel_learning();
      if (this->ir_.invalid_frame_count() > 0) {
        this->result_title_ = "Invalid Signal";
        this->result_detail_ = "No valid capture";
      } else {
        this->result_title_ = "No Signal";
        this->result_detail_ = "30 second timeout";
      }
      this->enter_state_(AppState::RESULT, now);
    }
  }
  if (this->state_ == AppState::RESULT && this->result_auto_return_ &&
      now - this->state_entered_at_ >= SENT_AUTO_RETURN_MS) {
    ESP_LOGI(TAG, "[APP] Sent status timeout; returning Home");
    this->enter_state_(AppState::HOME, now);
  }
  this->handle_event_(this->buttons_.update(now));
  if (this->state_ == AppState::ACTION_MENU && now - this->state_entered_at_ >= 15000) {
    ESP_LOGI(TAG, "[APP] Action menu timeout");
    this->enter_state_(AppState::HOME, now);
  }
  if (now - this->last_status_refresh_ >= 500 &&
      (this->state_ == AppState::HOME || this->state_ == AppState::LEARNING)) {
    this->last_status_refresh_ = now;
    this->refresh_screen_();
  }
}

void AppController::handle_event_(ButtonEvent event) {
  const uint32_t now = millis();
  if (event == ButtonEvent::NONE)
    return;

  if (event == ButtonEvent::SHORT_B) {
    if (this->state_ == AppState::HOME) {
      this->selected_slot_ = (this->selected_slot_ + 1) % 3;
      ESP_LOGI(TAG, "[APP] B: selected slot %u", this->selected_slot_ + 1);
    } else if (this->state_ == AppState::ACTION_MENU) {
      this->selected_action_ = (this->selected_action_ + 1) % 3;
      this->state_entered_at_ = now;
      static const char *const actions[3] = {"Send", "Learn", "Cancel"};
      ESP_LOGI(TAG, "[APP] B: selected action %s", actions[this->selected_action_]);
    } else if (this->state_ == AppState::SAVE_CONFIRM) {
      this->confirm_choice_ = (this->confirm_choice_ + 1) % 3;
      static const char *const choices[3] = {"Save", "Retry", "Cancel"};
      ESP_LOGI(TAG, "[APP] B: selected %s", choices[this->confirm_choice_]);
    }
    this->refresh_screen_();
    return;
  }

  if (event == ButtonEvent::SHORT_A) {
    if (this->state_ == AppState::HOME) {
      this->selected_action_ = 0;
      ESP_LOGI(TAG, "[APP] A: open action menu for slot %u", this->selected_slot_ + 1);
      this->enter_state_(AppState::ACTION_MENU, now);
    } else if (this->state_ == AppState::ACTION_MENU) {
      if (this->selected_action_ == 2) {
        ESP_LOGI(TAG, "[APP] A: cancel action menu");
        this->enter_state_(AppState::HOME, now);
        return;
      }
      if (this->selected_action_ == 1) {
        ESP_LOGI(TAG, "[APP] A: start learning slot %u", this->selected_slot_ + 1);
        this->start_learning_(now);
      } else {
        this->send_selected_(now);
      }
    } else if (this->state_ == AppState::NOTICE) {
      ESP_LOGI(TAG, "[APP] A: return from reserved feature notice");
      this->enter_state_(AppState::HOME, now);
    } else if (this->state_ == AppState::LEARNING) {
      this->ir_.cancel_learning();
      this->enter_state_(AppState::HOME, now);
    } else if (this->state_ == AppState::SAVE_CONFIRM) {
      if (this->confirm_choice_ == 0) {
        if (this->storage_.save_slot(this->selected_slot_, *this->pending_profile_)) {
          this->result_title_ = "Saved";
          this->result_detail_ = "Profile verified";
        } else {
          this->result_title_ = "Save Error";
          this->result_detail_ = "Old profile kept";
        }
        this->enter_state_(AppState::RESULT, now);
      } else if (this->confirm_choice_ == 1) {
        this->start_learning_(now);
      } else {
        this->enter_state_(AppState::HOME, now);
      }
    } else if (this->state_ == AppState::RESULT) {
      this->enter_state_(AppState::HOME, now);
    }
    return;
  }

  switch (event) {
    case ButtonEvent::BOTH_LONG:
      ESP_LOGI(TAG, "[APP] Both long is unused; use B to select and A to confirm");
      break;
    case ButtonEvent::LONG_A:
    case ButtonEvent::LONG_B:
      ESP_LOGI(TAG, "[APP] Individual long press is reserved");
      break;
    case ButtonEvent::SHORT_A:
    case ButtonEvent::SHORT_B:
    case ButtonEvent::NONE:
      break;
  }
}

void AppController::refresh_screen_() {
  if (this->state_ == AppState::ACTION_MENU) {
    this->screen_.render_action_menu(this->selected_slot_, this->selected_action_);
    return;
  }
  if (this->state_ == AppState::NOTICE) {
    this->screen_.render_notice(this->selected_slot_, this->selected_action_);
    return;
  }
  if (this->state_ == AppState::LEARNING) {
    const uint32_t elapsed = millis() - this->state_entered_at_;
    const uint8_t seconds_left =
        elapsed >= LEARNING_TIMEOUT_MS
            ? 0
            : (LEARNING_TIMEOUT_MS - elapsed + 999) / 1000;
    this->screen_.render_learning(this->selected_slot_, seconds_left,
                                  this->ir_.invalid_frame_count(),
                                  this->ir_.noise_event_count(),
                                  this->ir_.matched_frame_count(),
                                  this->ir_.candidate_protocol_name());
    return;
  }
  if (this->state_ == AppState::SAVE_CONFIRM) {
    this->screen_.render_save_confirm(this->selected_slot_, *this->pending_profile_,
                                      this->confirm_choice_);
    return;
  }
  if (this->state_ == AppState::SENDING) {
    this->screen_.render_sending(this->selected_slot_, this->pending_profile_->pulse_count,
                                 this->pending_profile_->carrier_hz);
    return;
  }
  if (this->state_ == AppState::RESULT) {
    this->screen_.render_result(this->result_title_, this->result_detail_);
    return;
  }
  const bool wifi_connected = network::is_connected();
  const bool api_connected =
      api::global_api_server != nullptr && api::global_api_server->is_connected();
  const bool learned[3] = {
      this->storage_.is_learned(0),
      this->storage_.is_learned(1),
      this->storage_.is_learned(2),
  };
  this->screen_.render_home(this->selected_slot_, learned, wifi_connected, api_connected);
}

void AppController::enter_state_(AppState state, uint32_t now_ms) {
  if (state != AppState::RESULT)
    this->result_auto_return_ = false;
  this->state_ = state;
  this->state_entered_at_ = now_ms;
  this->refresh_screen_();
}

void AppController::start_learning_(uint32_t now_ms) {
  if (!this->hardware_.prepare_ir_receive()) {
    this->result_title_ = "Power Error";
    this->result_detail_ = "Speaker amp state invalid";
    this->enter_state_(AppState::RESULT, now_ms);
    return;
  }
  if (!this->ir_.start_learning()) {
    this->result_title_ = "RX Error";
    this->result_detail_ = "Could not start RMT";
    this->enter_state_(AppState::RESULT, now_ms);
    return;
  }
  this->enter_state_(AppState::LEARNING, now_ms);
}

bool AppController::request_send_slot(uint8_t slot) {
  static const char *const slots[3] = {"Cool", "Heat", "Off"};
  if (slot >= 3 || this->is_failed() || this->pending_profile_ == nullptr) {
    ESP_LOGE(TAG, "[API] Rejected invalid slot request=%u", slot);
    return false;
  }

  // Learning and save confirmation both own pending_profile_. Overwriting it
  // with a stored slot would silently destroy an unsaved capture. An active
  // transmission must also finish before the shared TX buffer can be reused.
  if (this->state_ == AppState::LEARNING ||
      this->state_ == AppState::SAVE_CONFIRM ||
      this->state_ == AppState::SENDING) {
    ESP_LOGW(TAG, "[API] %s rejected: IRVault is busy", slots[slot]);
    return false;
  }

  this->selected_slot_ = slot;
  this->selected_action_ = 0;
  ESP_LOGI(TAG, "[API] %s button requested slot %u", slots[slot], slot + 1);
  return this->send_selected_(millis());
}

bool AppController::send_selected_(uint32_t now_ms) {
  if (!this->storage_.load_slot(this->selected_slot_, this->pending_profile_)) {
    ESP_LOGW(TAG, "[APP] Cannot send empty slot %u", this->selected_slot_ + 1);
    this->result_title_ = "Empty Slot";
    this->result_detail_ = "Learn this slot first";
    this->result_auto_return_ = false;
    this->enter_state_(AppState::RESULT, now_ms);
    return false;
  }

  ESP_LOGI(TAG, "[APP] Sending slot %u pulses=%u", this->selected_slot_ + 1,
           this->pending_profile_->pulse_count);
  this->enter_state_(AppState::SENDING, now_ms);
  if (!this->ir_.send(*this->pending_profile_)) {
    this->result_title_ = "Send Error";
    this->result_detail_ = "Profile not transmitted";
    this->result_auto_return_ = false;
    this->enter_state_(AppState::RESULT, millis());
    return false;
  }
  return true;
}

void AppController::dump_config() {
  ESP_LOGCONFIG(TAG, "IRVault long-frame build:");
  ESP_LOGCONFIG(TAG, "  States: HOME, ACTION_MENU, LEARNING, SAVE_CONFIRM, SENDING, RESULT");
  ESP_LOGCONFIG(TAG,
                "  Slots: Cool, Heat, Off; variable-length dual-copy RAW V2 storage");
  ESP_LOGCONFIG(TAG, "  GPIO11: Button A, active-low");
  ESP_LOGCONFIG(TAG, "  GPIO12: Button B, active-low");
  ESP_LOGCONFIG(TAG, "  Interaction: B selects, A confirms");
  ESP_LOGCONFIG(TAG,
                "  IR: native RMT RX/TX, max 2048 demodulated RAW durations, "
                "configured carrier defaults 38kHz");
  ESP_LOGCONFIG(TAG,
                "  Learning: Mitsubishi Electric 144-bit only; exact signature "
                "and checksum required; canonical 584-pulse output");
  ESP_LOGCONFIG(TAG, "  HA/API buttons: Cool, Heat, Off");
}

}  // namespace esphome::irvault
