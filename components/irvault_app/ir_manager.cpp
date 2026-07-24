#include "ir_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esphome/core/log.h"
#include "mitsubishi_codec.h"

namespace esphome::irvault {

static const char *const TAG = "irvault.ir";
static constexpr uint32_t RX_GLITCH_FILTER_NS = 1000;
static constexpr uint64_t MAX_FRAME_DURATION_US = 2000000;
static constexpr uint16_t MIN_VALID_PULSES = 20;

static uint32_t duration_abs(int32_t value) {
  return static_cast<uint32_t>(value < 0 ? -static_cast<int64_t>(value) : value);
}

static bool rmt_done(rmt_channel_handle_t, const rmt_rx_done_event_data_t *event, void *context) {
  static_cast<IRManager *>(context)->on_receive(event->num_symbols);
  // No task notification or semaphore is released from this ISR callback, so
  // there is no higher-priority task that needs an immediate context switch.
  return false;
}

static bool rmt_transmit_done(rmt_channel_handle_t,
                              const rmt_tx_done_event_data_t *,
                              void *context) {
  static_cast<IRManager *>(context)->on_transmit();
  return false;
}

bool IRManager::setup() {
  this->rx_symbols_ = static_cast<rmt_symbol_word_t *>(
      heap_caps_aligned_calloc(
          32, RMT_SYMBOL_CAPACITY, sizeof(rmt_symbol_word_t),
          MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  this->tx_symbols_ = static_cast<rmt_symbol_word_t *>(
      heap_caps_calloc(RMT_SYMBOL_CAPACITY, sizeof(rmt_symbol_word_t),
                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  this->capture_ = allocate_ir_profile();
  this->pending_candidate_ = allocate_ir_profile();
  this->working_candidate_ = allocate_ir_profile();
  if (this->rx_symbols_ == nullptr || this->tx_symbols_ == nullptr ||
      this->capture_ == nullptr || this->pending_candidate_ == nullptr ||
      this->working_candidate_ == nullptr) {
    ESP_LOGE(TAG, "[IR] Long-frame buffer allocation failed");
    return false;
  }

  rmt_rx_channel_config_t rx_config{};
  rx_config.gpio_num = GPIO_NUM_42;
  rx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rx_config.resolution_hz = 1000000;
  rx_config.mem_block_symbols = RMT_SYMBOL_CAPACITY;
  rx_config.flags.with_dma = true;
  esp_err_t rx_result = rmt_new_rx_channel(&rx_config, &this->rx_channel_);
  if (rx_result == ESP_OK) {
    this->rx_dma_enabled_ = true;
  } else {
    ESP_LOGW(TAG,
             "[IR] RX DMA unavailable (%s); falling back to native RMT memory",
             esp_err_to_name(rx_result));
    rx_config.mem_block_symbols = 128;
    rx_config.flags.with_dma = false;
    rx_result = rmt_new_rx_channel(&rx_config, &this->rx_channel_);
    if (rx_result != ESP_OK) {
      ESP_LOGE(TAG, "[IR] RX channel setup failed: %s",
               esp_err_to_name(rx_result));
      return false;
    }
  }
  rmt_rx_event_callbacks_t callbacks{};
  callbacks.on_recv_done = rmt_done;
  if (rmt_rx_register_event_callbacks(this->rx_channel_, &callbacks, this) != ESP_OK ||
      rmt_enable(this->rx_channel_) != ESP_OK)
    return false;

  rmt_tx_channel_config_t tx_config{};
  tx_config.gpio_num = GPIO_NUM_46;
  tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_config.resolution_hz = 1000000;
  tx_config.mem_block_symbols = 128;
  tx_config.trans_queue_depth = 1;
  if (rmt_new_tx_channel(&tx_config, &this->tx_channel_) != ESP_OK)
    return false;
  rmt_tx_event_callbacks_t tx_callbacks{};
  tx_callbacks.on_trans_done = rmt_transmit_done;
  if (rmt_tx_register_event_callbacks(this->tx_channel_, &tx_callbacks,
                                      this) != ESP_OK)
    return false;
  rmt_copy_encoder_config_t encoder_config{};
  if (rmt_new_copy_encoder(&encoder_config, &this->copy_encoder_) != ESP_OK ||
      rmt_enable(this->tx_channel_) != ESP_OK)
    return false;

  ESP_LOGI(TAG,
           "[IR] Native RMT RX GPIO42 and TX GPIO46 initialized "
           "capacity=%u pulses / %u symbols, RX backend=%s, profiles in PSRAM",
           MAX_RAW_DURATIONS, static_cast<unsigned>(RMT_SYMBOL_CAPACITY),
           this->rx_dma_enabled_ ? "DMA" : "native memory");
  return true;
}

bool IRManager::arm_() {
  rmt_receive_config_t config{};
  // ESP32-S3's RMT hardware glitch filter is limited to roughly 3.2 us with
  // this clock source. Keep the official StickS3 value here; longer receiver
  // glitches are rejected in software.
  config.signal_range_min_ns = RX_GLITCH_FILTER_NS;
  // 30 ms keeps common Mitsubishi Electric repeat gaps inside one capture
  // while staying below the 15-bit duration limit at 1 MHz.
  config.signal_range_max_ns = 30000000;
  return rmt_receive(this->rx_channel_, this->rx_symbols_,
                     RMT_SYMBOL_CAPACITY * sizeof(rmt_symbol_word_t),
                     &config) == ESP_OK;
}

bool IRManager::start_learning() {
  this->capture_ready_ = false;
  this->error_ = false;
  this->receive_done_ = false;
  this->invalid_frame_count_ = 0;
  this->noise_event_count_ = 0;
  this->rejection_diagnostic_dumped_ = false;
  this->matched_frame_count_ = 0;
  this->pending_protocol_ = CandidateProtocol::NONE;
  std::memset(this->pending_mitsubishi_state_, 0,
              sizeof(this->pending_mitsubishi_state_));
  this->learning_ = true;
  if (!this->arm_()) {
    this->error_ = true;
    this->learning_ = false;
  }
  ESP_LOGI(TAG, "[IR] Learning started");
  return !this->error_;
}

void IRManager::cancel_learning() {
  this->learning_ = false;
  this->capture_ready_ = false;
  this->matched_frame_count_ = 0;
  this->pending_protocol_ = CandidateProtocol::NONE;
  if (this->rx_channel_ != nullptr) {
    rmt_disable(this->rx_channel_);
    rmt_enable(this->rx_channel_);
  }
  ESP_LOGI(TAG, "[IR] Learning cancelled");
}

void IRManager::on_receive(size_t symbol_count) {
  this->received_symbols_ = symbol_count;
  this->receive_done_ = true;
}

void IRManager::on_transmit() { this->transmit_done_ = true; }

const char *IRManager::candidate_protocol_name() const {
  switch (this->pending_protocol_) {
    case CandidateProtocol::MITSUBISHI_AC:
      return "Mitsubishi AC";
    case CandidateProtocol::NONE:
    default:
      return "Signal";
  }
}

void IRManager::finalize_capture_() {
  this->pending_candidate_->carrier_hz = DEFAULT_CARRIER_HZ;
  const time_t now = time(nullptr);
  if (now > 1700000000) {
    this->pending_candidate_->created_timestamp = static_cast<uint64_t>(now);
    this->pending_candidate_->last_used_timestamp = static_cast<uint64_t>(now);
  }
  copy_ir_profile(this->capture_, *this->pending_candidate_);
}

void IRManager::log_capture_() const {
  uint64_t total = 0;
  uint32_t shortest = UINT32_MAX;
  uint32_t longest = 0;
  uint16_t long_spaces = 0;
  for (uint16_t index = 0; index < this->capture_->pulse_count; index++) {
    const uint32_t duration = duration_abs(this->capture_->raw[index]);
    total += duration;
    shortest = std::min(shortest, duration);
    longest = std::max(longest, duration);
    if (this->capture_->raw[index] > 0 && duration >= 5000)
      long_spaces++;
  }

  ESP_LOGI(TAG,
           "[IR] Confirmed Mitsubishi Electric frames=%u canonical_pulses=%u "
           "duration=%llu us shortest=%lu longest=%lu long_spaces=%u "
           "checksum=%02X carrier=configured 38000Hz",
           this->matched_frame_count_, this->capture_->pulse_count, total,
           static_cast<unsigned long>(shortest),
           static_cast<unsigned long>(longest), long_spaces,
           this->pending_mitsubishi_state_[MITSUBISHI_STATE_BYTES - 1]);

  char state_line[3 * MITSUBISHI_STATE_BYTES]{};
  size_t state_used = 0;
  for (size_t index = 0; index < MITSUBISHI_STATE_BYTES; index++) {
    const int written =
        snprintf(state_line + state_used, sizeof(state_line) - state_used,
                 "%s%02X", index == 0 ? "" : " ",
                 this->pending_mitsubishi_state_[index]);
    if (written <= 0 ||
        static_cast<size_t>(written) >= sizeof(state_line) - state_used)
      break;
    state_used += static_cast<size_t>(written);
  }
  ESP_LOGI(TAG, "[IR] Mitsubishi state: %s", state_line);

  // Long air-conditioner captures can contain hundreds of durations. Printing
  // the entire frame blocks ESPHome's loop and can disrupt Wi-Fi/API servicing,
  // so log complete short frames and a bounded head/tail for long ones.
  static constexpr uint16_t LOG_HEAD_PULSES = 72;
  static constexpr uint16_t LOG_TAIL_PULSES = 24;
  const bool abbreviated =
      this->capture_->pulse_count > LOG_HEAD_PULSES + LOG_TAIL_PULSES;
  const uint16_t head_end =
      abbreviated ? LOG_HEAD_PULSES : this->capture_->pulse_count;
  for (uint16_t offset = 0; offset < head_end; offset += 12) {
    char line[192]{};
    size_t used = 0;
    const uint16_t end =
        std::min<uint16_t>(head_end, offset + 12);
    for (uint16_t index = offset; index < end; index++) {
      const int written =
          snprintf(line + used, sizeof(line) - used, "%s%ld",
                   index == offset ? "" : ",",
                   static_cast<long>(this->capture_->raw[index]));
      if (written <= 0 ||
          static_cast<size_t>(written) >= sizeof(line) - used)
        break;
      used += static_cast<size_t>(written);
    }
    ESP_LOGI(TAG, "[IR] RAW[%u..%u]: %s", offset, end - 1, line);
  }
  if (abbreviated) {
    const uint16_t tail_start =
        this->capture_->pulse_count - LOG_TAIL_PULSES;
    ESP_LOGI(TAG, "[IR] RAW: ... %u middle pulses omitted from serial log ...",
             tail_start - head_end);
    for (uint16_t offset = tail_start;
         offset < this->capture_->pulse_count; offset += 12) {
      char line[192]{};
      size_t used = 0;
      const uint16_t end =
          std::min<uint16_t>(this->capture_->pulse_count, offset + 12);
      for (uint16_t index = offset; index < end; index++) {
        const int written =
            snprintf(line + used, sizeof(line) - used, "%s%ld",
                     index == offset ? "" : ",",
                     static_cast<long>(this->capture_->raw[index]));
        if (written <= 0 ||
            static_cast<size_t>(written) >= sizeof(line) - used)
          break;
        used += static_cast<size_t>(written);
      }
      ESP_LOGI(TAG, "[IR] RAW[%u..%u]: %s", offset, end - 1, line);
    }
  }
}

void IRManager::log_rejected_raw_(
    const IRProfile &candidate, const MitsubishiDecodeStats &stats) const {
  uint16_t under_100 = 0;
  uint16_t from_100_to_219 = 0;
  uint16_t from_220_to_249 = 0;
  uint16_t mark_count = 0;
  uint16_t space_count = 0;
  for (uint16_t index = 0; index < candidate.pulse_count; index++) {
    const uint32_t duration = duration_abs(candidate.raw[index]);
    mark_count += candidate.raw[index] < 0;
    space_count += candidate.raw[index] > 0;
    if (duration < 100)
      under_100++;
    else if (duration <= 219)
      from_100_to_219++;
    else if (duration <= 249)
      from_220_to_249++;
  }

  ESP_LOGW(TAG,
           "[IR][DIAG] First near-complete rejected RAW original=%u "
           "cleaned=%u marks=%u spaces=%u short_bins=<100:%u "
           "100..219:%u 220..249:%u",
           candidate.pulse_count, stats.cleaned_pulses, mark_count,
           space_count, under_100, from_100_to_219, from_220_to_249);
  for (uint16_t offset = 0; offset < candidate.pulse_count; offset += 12) {
    char line[192]{};
    size_t used = 0;
    const uint16_t end =
        std::min<uint16_t>(candidate.pulse_count, offset + 12);
    for (uint16_t index = offset; index < end; index++) {
      const int written =
          snprintf(line + used, sizeof(line) - used, "%s%ld",
                   index == offset ? "" : ",",
                   static_cast<long>(candidate.raw[index]));
      if (written <= 0 ||
          static_cast<size_t>(written) >= sizeof(line) - used)
        break;
      used += static_cast<size_t>(written);
    }
    ESP_LOGD(TAG, "[IR][DIAG] RAW[%u..%u]: %s", offset, end - 1, line);
  }
}

IRManager::CandidateResult IRManager::process_candidate_(size_t symbol_count) {
  if (symbol_count == 0 || symbol_count > RMT_SYMBOL_CAPACITY) {
    ESP_LOGD(TAG, "[IR] Rejected candidate: invalid symbol count=%u",
             static_cast<unsigned>(symbol_count));
    return CandidateResult::REJECTED;
  }

  uint16_t pulse_count = 0;
  uint64_t total = 0;
  reset_ir_profile(this->working_candidate_);
  IRProfile &candidate = *this->working_candidate_;
  for (size_t index = 0; index < symbol_count && pulse_count < MAX_RAW_DURATIONS; index++) {
    const auto &symbol = this->rx_symbols_[index];
    const int32_t values[2] = {
        symbol.level0 ? static_cast<int32_t>(symbol.duration0) : -static_cast<int32_t>(symbol.duration0),
        symbol.level1 ? static_cast<int32_t>(symbol.duration1) : -static_cast<int32_t>(symbol.duration1),
    };
    for (int32_t value : values) {
      if (value == 0)
        continue;
      candidate.raw[pulse_count++] = value;
      total += static_cast<uint32_t>(value < 0 ? -value : value);
    }
  }

  if (pulse_count < MIN_VALID_PULSES) {
    if (this->noise_event_count_ != UINT16_MAX)
      this->noise_event_count_++;
    ESP_LOGD(TAG, "[IR] Rejected candidate: too short pulses=%u duration=%llu us",
             pulse_count, total);
    return CandidateResult::REJECTED;
  }
  if (total < 10000 || total > MAX_FRAME_DURATION_US) {
    if (this->invalid_frame_count_ != UINT16_MAX)
      this->invalid_frame_count_++;
    ESP_LOGD(TAG, "[IR] Rejected candidate: duration out of range pulses=%u duration=%llu us",
             pulse_count, total);
    return CandidateResult::REJECTED;
  }
  candidate.pulse_count = pulse_count;
  candidate.carrier_hz = DEFAULT_CARRIER_HZ;
  // Preserve the original RMT waveform because the Mitsubishi decoder
  // sanitizes its working buffer in place.
  copy_ir_profile(this->pending_candidate_, candidate);

  MitsubishiDecodeStats stats{};
  uint8_t decoded_state[MITSUBISHI_STATE_BYTES]{};
  if (!decode_and_normalize_mitsubishi(
          candidate.raw, &candidate.pulse_count, MAX_RAW_DURATIONS,
          decoded_state, &stats)) {
    if (this->invalid_frame_count_ != UINT16_MAX)
      this->invalid_frame_count_++;
    ESP_LOGW(
        TAG,
        "[IR] Rejected Mitsubishi candidate input=%u short=%u cleaned=%u "
        "merged=%u edge_trim=%u short_marks=%u headers=%u data_candidates=%u "
        "suffix_candidates=%u signature_failures=%u checksum_failures=%u "
        "prefix=%u/144@%u bad_pair=%ld,%ld ambiguous=%s",
        stats.input_pulses, stats.input_short_pulses, stats.cleaned_pulses,
        stats.merged_glitches, stats.trimmed_edge_glitches,
        stats.preserved_short_marks,
        stats.header_candidates, stats.headerless_candidates,
        stats.suffix_candidates,
        stats.signature_failures, stats.checksum_failures,
        stats.longest_data_prefix_bits, stats.longest_data_prefix_start,
        static_cast<long>(stats.first_bad_mark),
        static_cast<long>(stats.first_bad_space),
        stats.ambiguous_valid_frames ? "YES" : "NO");
    if (!this->rejection_diagnostic_dumped_ &&
        stats.cleaned_pulses >= MITSUBISHI_DATA_PULSES) {
      this->rejection_diagnostic_dumped_ = true;
      this->log_rejected_raw_(*this->pending_candidate_, stats);
    }
    return CandidateResult::REJECTED;
  }

  copy_ir_profile(this->pending_candidate_, candidate);
  this->pending_protocol_ = CandidateProtocol::MITSUBISHI_AC;
  std::memcpy(this->pending_mitsubishi_state_, decoded_state,
              sizeof(this->pending_mitsubishi_state_));
  this->matched_frame_count_ =
      static_cast<uint8_t>(std::min<uint16_t>(stats.valid_frames, UINT8_MAX));
  ESP_LOGI(TAG,
           "[IR] Accepted checksum-valid Mitsubishi Electric frame(s)=%u "
           "recovered_without_header=%u input=%u cleaned=%u merged=%u "
           "edge_trim=%u short_marks=%u leading_byte_recovery=%u canonical=%u",
           stats.valid_frames, stats.recovered_frames, stats.input_pulses,
           stats.cleaned_pulses,
           stats.merged_glitches, stats.trimmed_edge_glitches,
           stats.preserved_short_marks,
           stats.leading_signature_byte_recoveries,
           candidate.pulse_count);
  this->finalize_capture_();
  this->log_capture_();
  return CandidateResult::COMPLETE;
}

void IRManager::update() {
  if (this->sending_ && this->transmit_done_) {
    this->transmit_done_ = false;
    this->sending_ = false;
    this->send_complete_ = true;
    ESP_LOGI(TAG,
             "[IR] Replay complete pulses=%u symbols=%u carrier=%luHz "
             "RX-low marks mapped to TX-high",
             this->transmitted_pulses_,
             static_cast<unsigned>(this->transmitted_symbols_),
             static_cast<unsigned long>(this->transmitted_carrier_hz_));
  }

  if (!this->learning_ || !this->receive_done_)
    return;
  this->receive_done_ = false;
  const CandidateResult result =
      this->process_candidate_(this->received_symbols_);
  if (result == CandidateResult::COMPLETE) {
    this->learning_ = false;
    this->capture_ready_ = true;
    return;
  }
  if (result == CandidateResult::REJECTED)
    ESP_LOGD(TAG, "[IR] Invalid candidate ignored; learning remains armed");
  if (!this->arm_()) {
    this->error_ = true;
    this->learning_ = false;
  }
}

bool IRManager::take_capture(IRProfile *profile) {
  if (!this->capture_ready_ || profile == nullptr)
    return false;
  copy_ir_profile(profile, *this->capture_);
  this->capture_ready_ = false;
  return true;
}

bool IRManager::take_send_complete() {
  if (!this->send_complete_)
    return false;
  this->send_complete_ = false;
  return true;
}

void IRManager::cancel_send() {
  this->sending_ = false;
  this->send_complete_ = false;
  this->transmit_done_ = false;
}

bool IRManager::prepare_tx_symbols_(const IRProfile &profile,
                                    uint16_t pulse_count,
                                    size_t *symbol_count) {
  if (symbol_count == nullptr || pulse_count < 20 ||
      pulse_count > profile.pulse_count ||
      profile.pulse_count > MAX_RAW_DURATIONS ||
      profile.carrier_hz < 20000 || profile.carrier_hz > 60000)
    return false;

  const size_t count = (pulse_count + 1U) / 2U;
  if (count > RMT_SYMBOL_CAPACITY)
    return false;

  uint64_t total = 0;
  for (uint16_t pulse = 0; pulse < pulse_count; pulse++) {
    const int32_t raw = profile.raw[pulse];
    const uint32_t duration =
        static_cast<uint32_t>(raw < 0 ? -static_cast<int64_t>(raw) : raw);
    if (duration == 0 || duration > 32767)
      return false;
    total += duration;
    auto &symbol = this->tx_symbols_[pulse / 2U];
    if ((pulse & 1U) == 0) {
      symbol = {};
      symbol.duration0 = duration;
      // The StickS3 demodulating receiver is active-low: captured negative
      // durations are IR marks. GPIO46 RMT transmission is active-high, so
      // marks must be converted to level 1 rather than replaying RX polarity.
      symbol.level0 = raw < 0;
    } else {
      symbol.duration1 = duration;
      symbol.level1 = raw < 0;
    }
  }
  if ((pulse_count & 1U) != 0) {
    auto &last = this->tx_symbols_[count - 1];
    last.duration1 = 0;
    last.level1 = 0;
  }
  if (total < 10000 || total > MAX_FRAME_DURATION_US)
    return false;

  *symbol_count = count;
  return true;
}

bool IRManager::send(const IRProfile &profile) {
  if (this->learning_ || this->sending_ || this->tx_channel_ == nullptr ||
      this->copy_encoder_ == nullptr) {
    ESP_LOGE(TAG, "[IR] Replay unavailable while RX or TX is active");
    return false;
  }

  const uint16_t transmit_pulses =
      mitsubishi_replay_pulse_count(profile.raw, profile.pulse_count);
  size_t symbol_count = 0;
  if (!this->prepare_tx_symbols_(profile, transmit_pulses, &symbol_count)) {
    ESP_LOGE(TAG, "[IR] Replay rejected invalid RAW profile");
    return false;
  }

  rmt_carrier_config_t carrier{};
  carrier.frequency_hz = profile.carrier_hz;
  carrier.duty_cycle = 0.33f;
  if (rmt_apply_carrier(this->tx_channel_, &carrier) != ESP_OK) {
    ESP_LOGE(TAG, "[IR] Failed to apply carrier %luHz",
             static_cast<unsigned long>(profile.carrier_hz));
    return false;
  }

  rmt_transmit_config_t transmit_config{};
  transmit_config.loop_count = 0;
  transmit_config.flags.eot_level = 0;
  this->transmit_done_ = false;
  this->send_complete_ = false;
  this->sending_ = true;
  this->transmitted_pulses_ = transmit_pulses;
  this->transmitted_symbols_ = symbol_count;
  this->transmitted_carrier_hz_ = profile.carrier_hz;
  const esp_err_t result =
      rmt_transmit(this->tx_channel_, this->copy_encoder_, this->tx_symbols_,
                   symbol_count * sizeof(rmt_symbol_word_t), &transmit_config);
  if (result != ESP_OK) {
    this->sending_ = false;
    ESP_LOGE(TAG, "[IR] Replay failed: %s", esp_err_to_name(result));
    return false;
  }

  ESP_LOGI(TAG,
           "[IR] Replay queued stored_pulses=%u tx_pulses=%u symbols=%u "
           "carrier=%luHz mode=%s",
           profile.pulse_count, transmit_pulses,
           static_cast<unsigned>(symbol_count),
           static_cast<unsigned long>(profile.carrier_hz),
           transmit_pulses < profile.pulse_count ? "Mitsubishi single-frame"
                                                 : "RAW unchanged");
  return true;
}

}  // namespace esphome::irvault
