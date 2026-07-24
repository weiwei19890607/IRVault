#pragma once

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esphome/core/component.h"

namespace esphome::official_tx_probe {

/** Repeats the official fixed NEC waveform to verify the StickS3 TX hardware path. */
class OfficialTxProbe : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

 protected:
  bool transmit_();
  void build_nec_();

  rmt_channel_handle_t channel_{nullptr};
  rmt_encoder_handle_t encoder_{nullptr};
  rmt_symbol_word_t symbols_[34]{};
  uint32_t last_transmit_{0};
  uint32_t last_power_report_{0};
};

}  // namespace esphome::official_tx_probe
