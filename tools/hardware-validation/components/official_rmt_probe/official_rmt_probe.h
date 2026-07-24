#pragma once

#include <cstddef>

#include "driver/rmt_rx.h"
#include "esphome/core/component.h"

namespace esphome::official_rmt_probe {

class OfficialRmtProbe : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void on_receive(size_t symbol_count);

 protected:
  bool start_receive_();

  rmt_channel_handle_t channel_{nullptr};
  rmt_symbol_word_t symbols_[256]{};
  volatile bool receive_done_{false};
  volatile size_t received_symbols_{0};
};

}  // namespace esphome::official_rmt_probe
