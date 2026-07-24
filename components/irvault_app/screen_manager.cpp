#include "screen_manager.h"

#include <M5Unified.h>

namespace esphome::irvault {

static constexpr uint16_t COLOR_BG = 0x0862;
static constexpr uint16_t COLOR_HEADER = 0x10A3;
static constexpr uint16_t COLOR_PANEL = 0x18E4;
static constexpr uint16_t COLOR_PANEL_SELECTED = 0x0455;
static constexpr uint16_t COLOR_ACCENT = 0x3DDF;
static constexpr uint16_t COLOR_MUTED = 0x9CF3;
static constexpr uint16_t COLOR_SUCCESS = 0x47E9;
static constexpr uint16_t COLOR_DANGER = 0xF986;

void ScreenManager::setup() {
  M5.Display.setRotation(0);
  M5.Display.setTextWrap(false);
}

void ScreenManager::begin_page_(const char *title) {
  auto &display = M5.Display;
  display.fillScreen(COLOR_BG);
  display.fillRoundRect(4, 4, 127, 34, 8, COLOR_HEADER);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, COLOR_HEADER);
  display.setCursor(10, 12);
  display.print(title);
  this->draw_battery_();
}

void ScreenManager::draw_footer_(const char *left, const char *right) {
  auto &display = M5.Display;
  display.fillRoundRect(5, 207, 125, 28, 7, COLOR_HEADER);
  display.setTextSize(1);
  display.setTextColor(COLOR_MUTED, COLOR_HEADER);
  display.setCursor(11, 217);
  display.print(left);
  if (right != nullptr) {
    const int x = 126 - static_cast<int>(strlen(right)) * 6;
    display.setCursor(x, 217);
    display.print(right);
  }
}

void ScreenManager::draw_choice_(int y, const char *label, bool selected) {
  auto &display = M5.Display;
  const uint16_t fill = selected ? COLOR_PANEL_SELECTED : COLOR_PANEL;
  display.fillRoundRect(7, y, 121, 37, 8, fill);
  if (selected)
    display.drawRoundRect(7, y, 121, 37, 8, COLOR_ACCENT);
  display.setTextSize(2);
  display.setTextColor(selected ? TFT_WHITE : COLOR_MUTED, fill);
  display.setCursor(17, y + 11);
  display.print(label);
  if (selected) {
    display.fillTriangle(116, y + 13, 116, y + 25, 122, y + 19, COLOR_ACCENT);
  }
}

void ScreenManager::draw_battery_() {
  auto &display = M5.Display;
  const uint16_t color =
      this->battery_level_ >= 0 && this->battery_level_ <= 15 ? COLOR_DANGER : COLOR_ACCENT;
  display.fillRoundRect(99, 8, 28, 22, 6, COLOR_BG);
  display.drawRoundRect(99, 8, 28, 22, 6, color);
  display.setTextSize(1);
  display.setTextColor(color, COLOR_BG);
  if (this->battery_level_ < 0) {
    display.setCursor(104, 16);
    display.print("--");
  } else {
    const int x = this->battery_level_ == 100 ? 104 : (this->battery_level_ >= 10 ? 107 : 110);
    display.setCursor(x, 16);
    display.print(this->battery_level_);
  }
  display.fillRect(127, 15, 3, 8, color);
}

void ScreenManager::set_battery_level(int level) {
  if (level < -1)
    level = -1;
  if (level > 100)
    level = 100;
  if (this->battery_level_ == level)
    return;
  this->battery_level_ = level;
  if (this->view_ != View::NONE)
    this->draw_battery_();
}

void ScreenManager::render_home(uint8_t selected_slot, const bool learned[3],
                                bool wifi_connected, bool api_connected) {
  if (this->view_ == View::HOME && this->selected_slot_ == selected_slot &&
      this->wifi_connected_ == wifi_connected && this->api_connected_ == api_connected &&
      this->learned_[0] == learned[0] && this->learned_[1] == learned[1] &&
      this->learned_[2] == learned[2])
    return;

  this->view_ = View::HOME;
  this->selected_slot_ = selected_slot;
  this->wifi_connected_ = wifi_connected;
  this->api_connected_ = api_connected;
  for (uint8_t index = 0; index < 3; index++)
    this->learned_[index] = learned[index];

  this->begin_page_("IRVault");
  auto &display = M5.Display;

  display.setTextSize(1);
  display.setTextColor(wifi_connected ? COLOR_SUCCESS : COLOR_DANGER, COLOR_BG);
  display.fillCircle(12, 48, 3, wifi_connected ? COLOR_SUCCESS : COLOR_DANGER);
  display.setCursor(20, 45);
  display.print("WiFi");
  display.setTextColor(api_connected ? COLOR_SUCCESS : COLOR_MUTED, COLOR_BG);
  display.fillCircle(72, 48, 3, api_connected ? COLOR_SUCCESS : COLOR_MUTED);
  display.setCursor(80, 45);
  display.print("HA");

  static const char *const labels[3] = {"Cool", "Heat", "Off"};
  for (uint8_t index = 0; index < 3; index++) {
    const int y = 61 + index * 44;
    const bool selected = index == selected_slot;
    const uint16_t fill = selected ? COLOR_PANEL_SELECTED : COLOR_PANEL;
    display.fillRoundRect(7, y, 121, 37, 8, fill);
    if (selected)
      display.drawRoundRect(7, y, 121, 37, 8, COLOR_ACCENT);
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, fill);
    display.setCursor(15, y + 7);
    display.print(labels[index]);
    display.setTextSize(1);
    display.setTextColor(learned[index] ? COLOR_SUCCESS : COLOR_MUTED, fill);
    display.setCursor(76, y + 12);
    display.print(learned[index] ? "READY" : "EMPTY");
    if (selected)
      display.fillTriangle(116, y + 13, 116, y + 25, 122, y + 19, COLOR_ACCENT);
  }

  this->draw_footer_("B  NEXT", "A  OPEN");
}

void ScreenManager::render_learning(uint8_t selected_slot, uint8_t seconds_left,
                                    uint16_t invalid_frames, uint16_t noise_events,
                                    uint8_t matched_frames, const char *protocol) {
  auto &display = M5.Display;
  if (this->view_ == View::LEARNING && this->selected_slot_ == selected_slot) {
    if (this->learning_seconds_left_ == seconds_left &&
        this->learning_invalid_frames_ == invalid_frames &&
        this->learning_noise_events_ == noise_events &&
        this->learning_matched_frames_ == matched_frames &&
        this->learning_protocol_ == protocol)
      return;
    this->learning_seconds_left_ = seconds_left;
    this->learning_invalid_frames_ = invalid_frames;
    this->learning_noise_events_ = noise_events;
    this->learning_matched_frames_ = matched_frames;
    this->learning_protocol_ = protocol;
    display.fillRect(12, 123, 111, 12, COLOR_PANEL);
    display.setTextSize(1);
    if (matched_frames > 0) {
      display.setTextColor(COLOR_SUCCESS, COLOR_PANEL);
      display.setCursor(12, 125);
      display.printf("%s accepted", protocol);
    } else if (invalid_frames > 0) {
      display.setTextColor(COLOR_DANGER, COLOR_PANEL);
      display.setCursor(12, 125);
      display.printf("Invalid %u - retry", invalid_frames);
    } else if (noise_events > 0) {
      display.setTextColor(COLOR_MUTED, COLOR_PANEL);
      display.setCursor(12, 125);
      display.print("Noise ignored");
    } else {
      display.setTextColor(COLOR_MUTED, COLOR_PANEL);
      display.setCursor(12, 125);
      display.print("Waiting for signal");
    }
    display.fillRoundRect(34, 151, 67, 35, 9, COLOR_PANEL_SELECTED);
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, COLOR_PANEL_SELECTED);
    display.setCursor(seconds_left >= 10 ? 47 : 53, 161);
    display.printf("%us", seconds_left);
    return;
  }

  this->view_ = View::LEARNING;
  this->selected_slot_ = selected_slot;
  this->learning_seconds_left_ = seconds_left;
  this->learning_invalid_frames_ = invalid_frames;
  this->learning_noise_events_ = noise_events;
  this->learning_matched_frames_ = matched_frames;
  this->learning_protocol_ = protocol;
  static const char *const slots[3] = {"Cool", "Heat", "Off"};
  this->begin_page_("Learn");

  display.fillRoundRect(7, 49, 121, 91, 10, COLOR_PANEL);
  display.setTextSize(2);
  display.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  display.setCursor(16, 61);
  display.print(slots[selected_slot]);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, COLOR_PANEL);
  display.setCursor(16, 91);
  display.print("Aim original remote");
  display.setCursor(16, 109);
  display.print("Press key once");
  display.setTextColor(COLOR_MUTED, COLOR_PANEL);
  display.setCursor(12, 125);
  if (matched_frames > 0) {
    display.setTextColor(COLOR_SUCCESS, COLOR_PANEL);
    display.printf("%s accepted", protocol);
  } else if (invalid_frames > 0) {
    display.setTextColor(COLOR_DANGER, COLOR_PANEL);
    display.printf("Invalid %u - retry", invalid_frames);
  } else if (noise_events > 0) {
    display.print("Noise ignored");
  } else {
    display.print("Waiting for signal");
  }

  display.fillRoundRect(34, 151, 67, 35, 9, COLOR_PANEL_SELECTED);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, COLOR_PANEL_SELECTED);
  display.setCursor(seconds_left >= 10 ? 47 : 53, 161);
  display.printf("%us", seconds_left);
  this->draw_footer_("LISTENING", "A  CANCEL");
}

void ScreenManager::render_save_confirm(uint8_t selected_slot, const IRProfile &profile,
                                        uint8_t choice) {
  this->view_ = View::SAVE_CONFIRM;
  static const char *const slots[3] = {"Cool", "Heat", "Off"};
  static const char *const choices[3] = {"Save", "Retry", "Cancel"};
  this->begin_page_("Signal");
  auto &display = M5.Display;

  display.setTextSize(1);
  display.setTextColor(COLOR_SUCCESS, COLOR_BG);
  display.setCursor(10, 47);
  display.print("CAPTURED");
  display.setTextColor(COLOR_MUTED, COLOR_BG);
  display.setCursor(66, 47);
  display.printf("%s / %u", slots[selected_slot], profile.pulse_count);

  for (uint8_t index = 0; index < 3; index++)
    this->draw_choice_(64 + index * 43, choices[index], index == choice);
  this->draw_footer_("B  NEXT", "A  CONFIRM");
}

void ScreenManager::render_sending(uint8_t selected_slot, uint16_t pulse_count,
                                   uint32_t carrier_hz) {
  this->view_ = View::SENDING;
  static const char *const slots[3] = {"Cool", "Heat", "Off"};
  this->begin_page_("Send");
  auto &display = M5.Display;

  display.fillRoundRect(7, 54, 121, 114, 11, COLOR_PANEL);
  display.fillCircle(67, 82, 12, COLOR_ACCENT);
  display.fillCircle(67, 82, 5, COLOR_PANEL);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, COLOR_PANEL);
  display.setCursor(15, 108);
  display.printf("%s...", slots[selected_slot]);
  display.setTextSize(1);
  display.setTextColor(COLOR_MUTED, COLOR_PANEL);
  display.setCursor(15, 138);
  display.printf("%u pulses / %lu kHz", pulse_count,
                 static_cast<unsigned long>(carrier_hz / 1000U));
  this->draw_footer_("TRANSMITTING", nullptr);
}

void ScreenManager::render_result(const char *title, const char *detail) {
  this->view_ = View::RESULT;
  this->begin_page_("Status");
  auto &display = M5.Display;

  display.fillRoundRect(7, 55, 121, 116, 11, COLOR_PANEL);
  const bool success = strcmp(title, "Saved") == 0 || strcmp(title, "Sent") == 0;
  display.fillCircle(67, 82, 13, success ? COLOR_SUCCESS : COLOR_DANGER);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, COLOR_PANEL);
  display.setCursor(15, 109);
  display.print(title);
  display.setTextSize(1);
  display.setTextColor(COLOR_MUTED, COLOR_PANEL);
  display.setCursor(15, 142);
  display.print(detail);
  if (strcmp(title, "Sent") == 0)
    this->draw_footer_("RETURNING", "1.5s");
  else
    this->draw_footer_("READY", "A  BACK");
}

void ScreenManager::render_action_menu(uint8_t selected_slot, uint8_t selected_action) {
  if (this->view_ == View::ACTION_MENU && this->selected_slot_ == selected_slot &&
      this->selected_action_ == selected_action)
    return;

  this->view_ = View::ACTION_MENU;
  this->selected_slot_ = selected_slot;
  this->selected_action_ = selected_action;
  static const char *const slots[3] = {"Cool", "Heat", "Off"};
  static const char *const actions[3] = {"Send", "Learn", "Cancel"};
  this->begin_page_(slots[selected_slot]);
  auto &display = M5.Display;
  display.setTextSize(1);
  display.setTextColor(COLOR_MUTED, COLOR_BG);
  display.setCursor(9, 47);
  display.print("CHOOSE ACTION");
  for (uint8_t index = 0; index < 3; index++)
    this->draw_choice_(64 + index * 43, actions[index], index == selected_action);
  this->draw_footer_("B  NEXT", "A  CONFIRM");
}

void ScreenManager::render_notice(uint8_t selected_slot, uint8_t selected_action) {
  this->view_ = View::NOTICE;
  this->selected_slot_ = selected_slot;
  this->selected_action_ = selected_action;
  static const char *const slots[3] = {"Cool", "Heat", "Off"};
  static const char *const actions[2] = {"Send", "Learn"};
  this->begin_page_("Notice");
  auto &display = M5.Display;
  display.fillRoundRect(7, 58, 121, 104, 11, COLOR_PANEL);
  display.setTextSize(2);
  display.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  display.setCursor(15, 76);
  display.print(actions[selected_action]);
  display.setTextColor(TFT_WHITE, COLOR_PANEL);
  display.setCursor(15, 105);
  display.print(slots[selected_slot]);
  display.setTextSize(1);
  display.setTextColor(COLOR_MUTED, COLOR_PANEL);
  display.setCursor(15, 137);
  display.print("Feature unavailable");
  this->draw_footer_("NOTICE", "A  BACK");
}

}  // namespace esphome::irvault
