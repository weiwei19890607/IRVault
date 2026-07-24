#include "sticks3_hardware.h"

#include <Arduino.h>
#include <ESP.h>

#include <M5Unified.h>

#include "esphome/core/log.h"

namespace esphome::sticks3_hardware {

static const char *const TAG = "sticks3.phase0";
static constexpr gpio_num_t KEY1_PIN = GPIO_NUM_11;
static constexpr gpio_num_t KEY2_PIN = GPIO_NUM_12;
float StickS3Hardware::get_setup_priority() const {
  // Run before the RMT receiver and networking, but after the logger is available.
  return setup_priority::BUS;
}

void StickS3Hardware::setup() {
  ESP_LOGI(TAG, "[Phase0] Selected Test %c", 'A' + static_cast<int>(this->test_mode_));
  this->log_memory_info_();
  // USB Serial/JTAG can re-enumerate while the host reconnects after boot.
  // Repeat the small hardware report so Test A evidence is not lost.
  this->set_interval("phase0_memory_report", 30000, [this]() { this->log_memory_info_(); });

  if (this->test_mode_ == TestMode::TEST_A)
    return;

  if (!this->initialize_m5_() || !this->initialize_pm1_()) {
    this->mark_failed();
    return;
  }

  switch (this->test_mode_) {
    case TestMode::TEST_B:
      this->setup_display_test_();
      break;
    case TestMode::TEST_C:
      this->setup_button_test_();
      break;
    case TestMode::TEST_D:
      if (!this->disable_speaker_amplifier_()) {
        ESP_LOGE(TAG, "[Phase0:D] Speaker amplifier disable failed; IR test must not proceed");
        this->mark_failed();
        return;
      }
      // The IR circuits require the StickS3 external 5 V rail. M5Unified owns PMIC power setup.
      M5.Power.setExtOutput(true, m5::ext_none);
      ESP_LOGI(TAG, "[Phase0:D] Speaker ended via M5Unified and EXT_5V enabled");
      ESP_LOGI(TAG, "[Phase0:D] ESPHome RMT receiver may now start on GPIO42");
      ESP_LOGW(TAG, "[Phase0:D] RX is demodulated; carrier measurement is not claimed");
      break;
    case TestMode::TEST_A:
      break;
  }
}

void StickS3Hardware::loop() {
  if (this->test_mode_ == TestMode::TEST_C && !this->is_failed())
    this->poll_buttons_();
}

void StickS3Hardware::dump_config() {
  ESP_LOGCONFIG(TAG, "StickS3 Phase 0 feasibility adapter:");
  ESP_LOGCONFIG(TAG, "  Test: %c", 'A' + static_cast<int>(this->test_mode_));
  ESP_LOGCONFIG(TAG, "  M5Unified initialized: %s", YESNO(this->m5_initialized_));
  ESP_LOGCONFIG(TAG, "  integrated M5PM1 driver initialized: %s", YESNO(this->pm1_initialized_));
}

void StickS3Hardware::log_memory_info_() {
  const uint32_t flash = ESP.getFlashChipSize();
  const uint32_t psram = ESP.getPsramSize();
  ESP_LOGI(TAG, "[Phase0:A] Chip model=%s revision=%u cores=%u", ESP.getChipModel(),
           ESP.getChipRevision(), ESP.getChipCores());
  ESP_LOGI(TAG, "[Phase0:A] Flash detected=%lu bytes (expected 8388608)",
           static_cast<unsigned long>(flash));
  ESP_LOGI(TAG, "[Phase0:A] PSRAM detected=%lu bytes (expected 8388608)",
           static_cast<unsigned long>(psram));
  ESP_LOGI(TAG, "[Phase0:A] Free heap=%lu free PSRAM=%lu",
           static_cast<unsigned long>(ESP.getFreeHeap()),
           static_cast<unsigned long>(ESP.getFreePsram()));
  if (flash != 8U * 1024U * 1024U || psram != 8U * 1024U * 1024U)
    ESP_LOGW(TAG, "[Phase0:A] Detected memory does not match the StickS3 specification");
}

bool StickS3Hardware::initialize_m5_() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;  // ESPHome owns USB serial logging.
  // The official StickS3 IR example initializes the internal speaker through
  // M5.begin() and then calls M5.Speaker.end() before starting RMT reception.
  // Preserve that exact lifecycle only for Test D.
  cfg.internal_spk = this->test_mode_ == TestMode::TEST_D;
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.external_display_value = 0;
  cfg.output_power = false;  // Test D enables it only after the amplifier is confirmed off.
  cfg.clear_display = this->test_mode_ == TestMode::TEST_B;
  M5.begin(cfg);

  this->m5_initialized_ = M5.getBoard() == m5::board_t::board_M5StickS3;
  ESP_LOGI(TAG, "[Phase0] M5Unified board id=%d StickS3=%s", static_cast<int>(M5.getBoard()),
           YESNO(this->m5_initialized_));
  return this->m5_initialized_;
}

bool StickS3Hardware::initialize_pm1_() {
  // Use M5Unified's integrated M5PM1 driver and bus ownership. A separate
  // Arduino M5PM1 instance would install a competing Wire bus under ESPHome.
  const bool type_ok = M5.Power.getType() == m5::Power_Class::pmic_m5pm1;
  const bool begin_ok = type_ok && M5.Power.M5pm1.begin();
  this->pm1_initialized_ = type_ok && begin_ok;
  ESP_LOGI(TAG, "[Phase0] M5PM1 integrated type_ok=%s begin=%s", YESNO(type_ok), YESNO(begin_ok));
  return this->pm1_initialized_;
}

bool StickS3Hardware::disable_speaker_amplifier_() {
  // Follow the official StickS3 IR example. GPIO3 is a speaker pulse-control
  // signal, so treating it as a conventional level-enable is not sufficient.
  M5.Speaker.end();
  ESP_LOGI(TAG, "[Phase0:D] M5.Speaker.end() called before RMT receiver startup");
  return true;
}

void StickS3Hardware::setup_display_test_() {
  auto &display = M5.Display;
  display.setRotation(0);
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.setCursor(12, 20);
  display.println("IRVault");
  display.setTextColor(TFT_GREEN, TFT_BLACK);
  display.setCursor(12, 58);
  display.println("Phase 0 B");
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setCursor(12, 96);
  display.println("M5Unified/M5GFX");
  display.setCursor(12, 112);
  display.println("LCD static test");
  ESP_LOGI(TAG, "[Phase0:B] Static LCD test drawn through M5Unified/M5GFX");
}

void StickS3Hardware::setup_button_test_() {
  uint8_t mode = INPUT;
  const char *pull_name = "NONE";
  if (this->button_pull_ == ButtonPull::PULL_UP) {
    mode = INPUT_PULLUP;
    pull_name = "UP";
  } else if (this->button_pull_ == ButtonPull::PULL_DOWN) {
    mode = INPUT_PULLDOWN;
    pull_name = "DOWN";
  }
  pinMode(KEY1_PIN, mode);
  pinMode(KEY2_PIN, mode);
  ESP_LOGI(TAG, "[Phase0:C] GPIO11=KEY1 GPIO12=KEY2 pull=%s", pull_name);
  ESP_LOGW(TAG, "[Phase0:C] A/B physical mapping and active level require operator observation");
  this->poll_buttons_();
}

void StickS3Hardware::poll_buttons_() {
  const int key1 = digitalRead(KEY1_PIN);
  const int key2 = digitalRead(KEY2_PIN);
  if (key1 == this->last_key1_ && key2 == this->last_key2_)
    return;
  this->last_key1_ = key1;
  this->last_key2_ = key2;
  ESP_LOGI(TAG, "[Phase0:C] KEY1(GPIO11)=%d KEY2(GPIO12)=%d same_level=%s", key1, key2,
           YESNO(key1 == key2));
}

}  // namespace esphome::sticks3_hardware
