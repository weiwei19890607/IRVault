#include "official_tx_probe.h"

#include <Arduino.h>
#include <M5Unified.h>

#include "esphome/core/log.h"

namespace esphome::official_tx_probe {

static const char *const TAG = "sticks3.official_tx";

float OfficialTxProbe::get_setup_priority() const { return setup_priority::BUS; }

void OfficialTxProbe::build_nec_() {
  // Same fixed standard-NEC shape used by the M5Stack example. This test is
  // only an electrical TX probe; IRVault product code remains protocol-agnostic.
  const uint32_t raw = 0xAA55FF00;
  this->symbols_[0] = {
      .duration0 = 9000, .level0 = 1, .duration1 = 4500, .level1 = 0};
  for (uint8_t bit = 0; bit < 32; bit++) {
    this->symbols_[bit + 1] = {
        .duration0 = 560,
        .level0 = 1,
        .duration1 = static_cast<uint16_t>((raw & (1UL << bit)) ? 1690 : 560),
        .level1 = 0,
    };
  }
  this->symbols_[33] = {
      .duration0 = 560, .level0 = 1, .duration1 = 0, .level1 = 0};
}

void OfficialTxProbe::setup() {
  // Keep this control experiment independent from the RX-oriented Phase 0
  // adapter and follow the official TX example's M5.begin lifecycle.
  auto m5_config = M5.config();
  m5_config.serial_baudrate = 0;
  M5.begin(m5_config);

  rmt_tx_channel_config_t tx_config{};
  tx_config.gpio_num = GPIO_NUM_46;
  tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_config.resolution_hz = 1000000;
  tx_config.mem_block_symbols = 64;
  tx_config.trans_queue_depth = 4;
  tx_config.flags.invert_out = false;
  tx_config.flags.with_dma = false;
  esp_err_t result = rmt_new_tx_channel(&tx_config, &this->channel_);

  rmt_carrier_config_t carrier{};
  carrier.frequency_hz = 38000;
  carrier.duty_cycle = 0.33f;
  carrier.flags.polarity_active_low = false;
  if (result == ESP_OK)
    result = rmt_apply_carrier(this->channel_, &carrier);

  rmt_copy_encoder_config_t encoder_config{};
  if (result == ESP_OK)
    result = rmt_new_copy_encoder(&encoder_config, &this->encoder_);
  if (result == ESP_OK)
    result = rmt_enable(this->channel_);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "Official TX setup failed: %s", esp_err_to_name(result));
    this->mark_failed();
    return;
  }

  this->build_nec_();
  const bool ext_write_ok = M5.Power.M5pm1.setExtOutput(true);
  delay(100);
  const bool ext_enabled = M5.Power.M5pm1.getExtOutput();
  const uint16_t output_mv = M5.Power.M5pm1.get5VoutVoltage();
  ESP_LOGI(TAG, "Official TX probe initialized: GPIO46, 38kHz, 33%% duty");
  ESP_LOGI(TAG, "PMIC EXT_5V write=%s enabled=%s measured=%umV",
           YESNO(ext_write_ok), YESNO(ext_enabled), output_mv);
}

bool OfficialTxProbe::transmit_() {
  rmt_transmit_config_t config{};
  config.loop_count = 0;
  config.flags.eot_level = 0;
  esp_err_t result =
      rmt_transmit(this->channel_, this->encoder_, this->symbols_, sizeof(this->symbols_), &config);
  if (result == ESP_OK)
    result = rmt_tx_wait_all_done(this->channel_, 1000);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "Official TX failed: %s", esp_err_to_name(result));
    return false;
  }
  ESP_LOGI(TAG, "Official NEC test burst sent on GPIO46");
  return true;
}

void OfficialTxProbe::loop() {
  const uint32_t now = millis();
  if (now - this->last_power_report_ >= 10000) {
    this->last_power_report_ = now;
    ESP_LOGI(TAG, "PMIC EXT_5V enabled=%s measured=%umV",
             YESNO(M5.Power.M5pm1.getExtOutput()), M5.Power.M5pm1.get5VoutVoltage());
  }
  if (now - this->last_transmit_ < 2000)
    return;
  this->last_transmit_ = now;
  this->transmit_();
}

void OfficialTxProbe::dump_config() {
  ESP_LOGCONFIG(TAG, "Official M5Stack-style RMT TX electrical test:");
  ESP_LOGCONFIG(TAG, "  GPIO46, 1 us resolution, 38 kHz, 33%% duty");
  ESP_LOGCONFIG(TAG, "  Fixed NEC-shaped burst repeats every 2 seconds");
}

}  // namespace esphome::official_tx_probe
