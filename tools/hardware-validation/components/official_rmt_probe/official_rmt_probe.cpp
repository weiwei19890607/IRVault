#include "official_rmt_probe.h"

#include "esphome/core/log.h"

namespace esphome::official_rmt_probe {

static const char *const TAG = "sticks3.official_rmt";
static OfficialRmtProbe *active_probe = nullptr;

static bool rmt_rx_done_callback(rmt_channel_handle_t, const rmt_rx_done_event_data_t *event,
                                 void *user_data) {
  auto *probe = static_cast<OfficialRmtProbe *>(user_data);
  probe->on_receive(event->num_symbols);
  return true;
}

float OfficialRmtProbe::get_setup_priority() const {
  // StickS3Hardware uses BUS priority to shut down the amplifier and enable
  // EXT_5V. Create the RMT channel only after that hardware setup completes.
  return setup_priority::DATA;
}

void OfficialRmtProbe::setup() {
  active_probe = this;

  rmt_rx_channel_config_t rx_config{};
  rx_config.gpio_num = GPIO_NUM_42;
  rx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rx_config.resolution_hz = 1000000;
  rx_config.mem_block_symbols = 128;

  esp_err_t err = rmt_new_rx_channel(&rx_config, &this->channel_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_rx_channel failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  rmt_rx_event_callbacks_t callbacks{};
  callbacks.on_recv_done = rmt_rx_done_callback;
  err = rmt_rx_register_event_callbacks(this->channel_, &callbacks, this);
  if (err == ESP_OK)
    err = rmt_enable(this->channel_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RMT callback registration/enable failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Official-style native RMT receiver initialized on GPIO42");
  if (!this->start_receive_())
    this->mark_failed();
}

void OfficialRmtProbe::on_receive(size_t symbol_count) {
  this->received_symbols_ = symbol_count;
  this->receive_done_ = true;
}

bool OfficialRmtProbe::start_receive_() {
  rmt_receive_config_t receive_config{};
  receive_config.signal_range_min_ns = 1000;
  receive_config.signal_range_max_ns = 20000000;
  const esp_err_t err =
      rmt_receive(this->channel_, this->symbols_, sizeof(this->symbols_), &receive_config);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

void OfficialRmtProbe::loop() {
  if (!this->receive_done_)
    return;

  this->receive_done_ = false;
  const size_t count = this->received_symbols_;
  ESP_LOGI(TAG, "RX complete: %u RMT symbols / %u pulse durations",
           static_cast<unsigned>(count), static_cast<unsigned>(count * 2));

  for (size_t index = 0; index < count; index++) {
    const auto &symbol = this->symbols_[index];
    const int32_t pulse0 =
        symbol.level0 ? static_cast<int32_t>(symbol.duration0) : -static_cast<int32_t>(symbol.duration0);
    const int32_t pulse1 =
        symbol.level1 ? static_cast<int32_t>(symbol.duration1) : -static_cast<int32_t>(symbol.duration1);
    ESP_LOGI(TAG, "  [%03u] %ld, %ld", static_cast<unsigned>(index),
             static_cast<long>(pulse0), static_cast<long>(pulse1));
  }

  if (!this->start_receive_())
    this->mark_failed();
}

void OfficialRmtProbe::dump_config() {
  ESP_LOGCONFIG(TAG, "Official M5Stack-style native RMT control test:");
  ESP_LOGCONFIG(TAG, "  Pin: GPIO42");
  ESP_LOGCONFIG(TAG, "  Resolution: 1 us");
  ESP_LOGCONFIG(TAG, "  Minimum signal: 1 us");
  ESP_LOGCONFIG(TAG, "  Idle/end threshold: 20 ms");
  ESP_LOGCONFIG(TAG, "  Buffer: 256 symbols");
}

}  // namespace esphome::official_rmt_probe
