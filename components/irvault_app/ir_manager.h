#pragma once

#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "ir_profile.h"
#include "mitsubishi_codec.h"

namespace esphome::irvault {

static constexpr size_t RMT_SYMBOL_CAPACITY = (MAX_RAW_DURATIONS + 1U) / 2U;

/** Learns verified Mitsubishi Electric frames and replays stored RAW via RMT. */
class IRManager {
 public:
  bool setup();
  bool start_learning();
  void cancel_learning();
  bool send(const IRProfile &profile);
  void update();
  bool has_capture() const { return this->capture_ready_; }
  bool has_error() const { return this->error_; }
  bool take_capture(IRProfile *profile);
  void on_receive(size_t symbol_count);
  void on_transmit();
  bool take_send_complete();
  bool is_sending() const { return this->sending_; }
  void cancel_send();
  uint16_t invalid_frame_count() const { return this->invalid_frame_count_; }
  uint16_t noise_event_count() const { return this->noise_event_count_; }
  uint8_t matched_frame_count() const { return this->matched_frame_count_; }
  const char *candidate_protocol_name() const;

 protected:
  enum class CandidateProtocol : uint8_t {
    NONE,
    MITSUBISHI_AC,
  };

  enum class CandidateResult : uint8_t {
    REJECTED,
    PENDING,
    COMPLETE,
  };

  bool arm_();
  CandidateResult process_candidate_(size_t symbol_count);
  void finalize_capture_();
  void log_capture_() const;
  void log_rejected_raw_(const IRProfile &candidate,
                         const MitsubishiDecodeStats &stats) const;
  bool prepare_tx_symbols_(const IRProfile &profile, uint16_t pulse_count,
                           size_t *symbol_count);

  rmt_channel_handle_t rx_channel_{nullptr};
  rmt_channel_handle_t tx_channel_{nullptr};
  rmt_encoder_handle_t copy_encoder_{nullptr};
  rmt_symbol_word_t *rx_symbols_{nullptr};
  rmt_symbol_word_t *tx_symbols_{nullptr};
  IRProfile *capture_{nullptr};
  IRProfile *pending_candidate_{nullptr};
  IRProfile *working_candidate_{nullptr};
  volatile size_t received_symbols_{0};
  volatile bool receive_done_{false};
  volatile bool transmit_done_{false};
  bool learning_{false};
  bool capture_ready_{false};
  bool error_{false};
  bool sending_{false};
  bool send_complete_{false};
  uint16_t transmitted_pulses_{0};
  size_t transmitted_symbols_{0};
  uint32_t transmitted_carrier_hz_{0};
  CandidateProtocol pending_protocol_{CandidateProtocol::NONE};
  uint8_t pending_mitsubishi_state_[MITSUBISHI_STATE_BYTES]{};
  uint8_t matched_frame_count_{0};
  uint16_t invalid_frame_count_{0};
  uint16_t noise_event_count_{0};
  bool rx_dma_enabled_{false};
  bool rejection_diagnostic_dumped_{false};
};

}  // namespace esphome::irvault
