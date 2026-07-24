#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::irvault {

static constexpr size_t MITSUBISHI_STATE_BYTES = 18;
static constexpr uint16_t MITSUBISHI_DATA_PULSES =
    MITSUBISHI_STATE_BYTES * 8U * 2U + 1U;
static constexpr uint16_t MITSUBISHI_FRAME_PULSES = 291;
static constexpr uint16_t MITSUBISHI_REPEAT_PULSES = 292;
static constexpr uint16_t MITSUBISHI_CANONICAL_PULSES =
    MITSUBISHI_REPEAT_PULSES * 2;

struct MitsubishiDecodeStats {
  uint16_t input_pulses{0};
  uint16_t input_short_pulses{0};
  uint16_t cleaned_pulses{0};
  uint16_t merged_glitches{0};
  uint16_t trimmed_edge_glitches{0};
  uint16_t preserved_short_marks{0};
  uint16_t header_candidates{0};
  uint16_t headerless_candidates{0};
  uint16_t suffix_candidates{0};
  uint16_t leading_signature_byte_recoveries{0};
  uint16_t signature_failures{0};
  uint16_t checksum_failures{0};
  uint16_t longest_data_prefix_bits{0};
  uint16_t longest_data_prefix_start{0};
  int32_t first_bad_mark{0};
  int32_t first_bad_space{0};
  uint16_t valid_frames{0};
  uint16_t recovered_frames{0};
  bool ambiguous_valid_frames{false};
};

/**
 * Validate the StickS3-tested Mitsubishi Electric 144-bit state variant.
 *
 * This project deliberately accepts only the user's verified
 * 23 CB 26 01 01 signature and the protocol checksum. It does not attempt to
 * treat arbitrary long RAW captures as Mitsubishi data.
 */
bool validate_mitsubishi_state(
    const uint8_t state[MITSUBISHI_STATE_BYTES]);

/**
 * Encode two canonical 144-bit Mitsubishi Electric frames at nominal timings.
 *
 * Output polarity matches StickS3 RX storage: negative values are IR marks,
 * positive values are spaces. The caller supplies a buffer with at least
 * MITSUBISHI_CANONICAL_PULSES entries.
 */
bool encode_mitsubishi_state(
    const uint8_t state[MITSUBISHI_STATE_BYTES], int32_t *raw,
    uint16_t capacity, uint16_t *pulse_count);

/**
 * Select the number of RAW pulses to transmit.
 *
 * Learned Mitsubishi profiles remain stored as two identical validated
 * frames. For a normalized dual-frame profile, replay only the first
 * 292-pulse frame because the target indoor unit has been verified to act on
 * a single frame and audibly acknowledges two complete frames twice.
 * Unrecognized RAW profiles are returned unchanged.
 */
uint16_t mitsubishi_replay_pulse_count(const int32_t *raw,
                                       uint16_t pulse_count);

/**
 * Conservatively deglitch, scan, checksum-validate, and normalize a capture.
 *
 * On success, raw is replaced with two canonical frames and pulse_count is
 * MITSUBISHI_CANONICAL_PULSES. On failure, the caller must discard the
 * candidate; no unvalidated RAW data is suitable for persistence.
 */
bool decode_and_normalize_mitsubishi(
    int32_t *raw, uint16_t *pulse_count, uint16_t capacity,
    uint8_t state[MITSUBISHI_STATE_BYTES], MitsubishiDecodeStats *stats);

}  // namespace esphome::irvault
