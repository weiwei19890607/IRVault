#include "mitsubishi_codec.h"

#include <climits>
#include <cstring>

namespace esphome::irvault {

namespace {

static constexpr uint32_t GLITCH_MAX_US = 220;
static constexpr uint32_t MERGED_DURATION_MAX_US = 30000;

static constexpr uint32_t HEADER_MARK_MIN_US = 2400;
static constexpr uint32_t HEADER_MARK_MAX_US = 4800;
static constexpr uint32_t HEADER_SPACE_MIN_US = 1000;
static constexpr uint32_t HEADER_SPACE_MAX_US = 2600;
// StickS3's demodulating receiver can compress otherwise valid Mitsubishi
// data marks to roughly 119 us while leaving the data-bearing spaces intact.
// Marks do not encode the bit value, so accept the measured lower bound while
// retaining strict space timing, signature, length, and checksum validation.
static constexpr uint32_t BIT_MARK_MIN_US = 100;
static constexpr uint32_t BIT_MARK_MAX_US = 750;
static constexpr uint32_t ZERO_SPACE_MIN_US = 250;
static constexpr uint32_t ZERO_SPACE_MAX_US = 750;
static constexpr uint32_t ONE_SPACE_MIN_US = 850;
static constexpr uint32_t ONE_SPACE_MAX_US = 1900;

static constexpr int32_t CANONICAL_HEADER_MARK_US = -3400;
static constexpr int32_t CANONICAL_HEADER_SPACE_US = 1750;
static constexpr int32_t CANONICAL_BIT_MARK_US = -450;
static constexpr int32_t CANONICAL_ZERO_SPACE_US = 420;
static constexpr int32_t CANONICAL_ONE_SPACE_US = 1300;
static constexpr int32_t CANONICAL_FOOTER_MARK_US = -440;
static constexpr int32_t CANONICAL_REPEAT_GAP_US = 15500;

static constexpr uint8_t MITSUBISHI_SIGNATURE[5] = {
    0x23, 0xCB, 0x26, 0x01, 0x01,
};
static constexpr uint16_t MITSUBISHI_SUFFIX_BYTES =
    MITSUBISHI_STATE_BYTES - 1U;
static constexpr uint16_t MITSUBISHI_SUFFIX_DATA_PULSES =
    MITSUBISHI_SUFFIX_BYTES * 8U * 2U + 1U;

static bool signature_matches(
    const uint8_t state[MITSUBISHI_STATE_BYTES]) {
  return state != nullptr &&
         std::memcmp(state, MITSUBISHI_SIGNATURE,
                     sizeof(MITSUBISHI_SIGNATURE)) == 0;
}

static bool checksum_matches(
    const uint8_t state[MITSUBISHI_STATE_BYTES]) {
  if (state == nullptr)
    return false;
  uint8_t checksum = 0;
  for (size_t index = 0; index < MITSUBISHI_STATE_BYTES - 1; index++)
    checksum = static_cast<uint8_t>(checksum + state[index]);
  return checksum == state[MITSUBISHI_STATE_BYTES - 1];
}

static uint32_t duration_abs(int32_t value) {
  return static_cast<uint32_t>(
      value < 0 ? -static_cast<int64_t>(value) : value);
}

static bool same_polarity(int32_t first, int32_t second) {
  return (first < 0) == (second < 0);
}

static bool pulse_matches(int32_t pulse, bool mark, uint32_t minimum,
                          uint32_t maximum) {
  if ((pulse < 0) != mark)
    return false;
  const uint32_t duration = duration_abs(pulse);
  return duration >= minimum && duration <= maximum;
}

static uint16_t compact_adjacent_levels(int32_t *raw, uint16_t count) {
  uint16_t output = 0;
  for (uint16_t input = 0; input < count; input++) {
    const int32_t pulse = raw[input];
    if (pulse == 0)
      continue;
    if (output != 0 && same_polarity(raw[output - 1], pulse)) {
      const int64_t combined =
          static_cast<int64_t>(duration_abs(raw[output - 1])) +
          duration_abs(pulse);
      if (combined > INT32_MAX)
        return 0;
      raw[output - 1] =
          raw[output - 1] < 0 ? -static_cast<int32_t>(combined)
                              : static_cast<int32_t>(combined);
    } else {
      raw[output++] = pulse;
    }
  }
  return output;
}

static uint16_t sanitize_glitches(int32_t *raw, uint16_t count,
                                  MitsubishiDecodeStats *stats) {
  count = compact_adjacent_levels(raw, count);
  if (count == 0)
    return 0;

  // First repair short positive glitches that split a real active-low mark.
  // Ordering matters: with [-119, +154, -68], handling -119 first would merge
  // two data spaces; handling +154 first correctly reconstructs a -341 mark.
  uint16_t index = 1;
  while (index + 1 < count) {
    const uint32_t duration = duration_abs(raw[index]);
    if (raw[index] <= 0 || duration > GLITCH_MAX_US ||
        raw[index - 1] >= 0 || raw[index + 1] >= 0) {
      index++;
      continue;
    }

    const uint64_t combined =
        static_cast<uint64_t>(duration_abs(raw[index - 1])) +
        duration + duration_abs(raw[index + 1]);
    if (combined > MERGED_DURATION_MAX_US) {
      index++;
      continue;
    }

    raw[index - 1] = -static_cast<int32_t>(combined);
    std::memmove(raw + index, raw + index + 2,
                 static_cast<size_t>(count - index - 2) * sizeof(int32_t));
    count -= 2;
    stats->merged_glitches++;
    if (index > 1)
      index--;
  }
  count = compact_adjacent_levels(raw, count);

  index = 0;
  while (index < count) {
    if (duration_abs(raw[index]) > GLITCH_MAX_US) {
      index++;
      continue;
    }

    if (index == 0 || index + 1 >= count) {
      std::memmove(raw + index, raw + index + 1,
                   static_cast<size_t>(count - index - 1) * sizeof(int32_t));
      count--;
      stats->trimmed_edge_glitches++;
      if (index != 0)
        index--;
      continue;
    }

    // A shortened active-low mark may still separate two valid data spaces.
    // Deleting it would merge two bits into one long space. Positive pulses
    // this short cannot represent a Mitsubishi data space and remain eligible
    // for conservative three-pulse glitch merging below.
    if (raw[index] < 0 &&
        duration_abs(raw[index]) >= BIT_MARK_MIN_US) {
      stats->preserved_short_marks++;
      index++;
      continue;
    }

    if (!same_polarity(raw[index - 1], raw[index + 1])) {
      index++;
      continue;
    }

    const uint64_t combined =
        static_cast<uint64_t>(duration_abs(raw[index - 1])) +
        duration_abs(raw[index]) + duration_abs(raw[index + 1]);
    if (combined > MERGED_DURATION_MAX_US) {
      index++;
      continue;
    }

    raw[index - 1] =
        raw[index - 1] < 0 ? -static_cast<int32_t>(combined)
                           : static_cast<int32_t>(combined);
    std::memmove(raw + index, raw + index + 2,
                 static_cast<size_t>(count - index - 2) * sizeof(int32_t));
    count -= 2;
    stats->merged_glitches++;
    if (index > 1)
      index--;
  }

  return compact_adjacent_levels(raw, count);
}

static bool decode_data(const int32_t *raw, uint16_t count, uint16_t start,
                        uint8_t state[MITSUBISHI_STATE_BYTES]) {
  if (raw == nullptr || state == nullptr ||
      static_cast<uint32_t>(start) + MITSUBISHI_DATA_PULSES > count)
    return false;

  std::memset(state, 0, MITSUBISHI_STATE_BYTES);
  for (uint16_t bit = 0; bit < MITSUBISHI_STATE_BYTES * 8U; bit++) {
    const uint16_t mark_index = start + bit * 2;
    const uint16_t space_index = mark_index + 1;
    if (!pulse_matches(raw[mark_index], true, BIT_MARK_MIN_US,
                       BIT_MARK_MAX_US))
      return false;

    bool one = false;
    if (pulse_matches(raw[space_index], false, ZERO_SPACE_MIN_US,
                      ZERO_SPACE_MAX_US)) {
      one = false;
    } else if (pulse_matches(raw[space_index], false, ONE_SPACE_MIN_US,
                             ONE_SPACE_MAX_US)) {
      one = true;
    } else {
      return false;
    }
    if (one)
      state[bit / 8U] |= static_cast<uint8_t>(1U << (bit % 8U));
  }

  return pulse_matches(raw[start + MITSUBISHI_DATA_PULSES - 1], true,
                       BIT_MARK_MIN_US, BIT_MARK_MAX_US);
}

static bool decode_suffix_without_first_signature_byte(
    const int32_t *raw, uint16_t count, uint16_t start,
    uint8_t state[MITSUBISHI_STATE_BYTES]) {
  if (raw == nullptr || state == nullptr ||
      static_cast<uint32_t>(start) + MITSUBISHI_SUFFIX_DATA_PULSES > count)
    return false;

  std::memset(state, 0, MITSUBISHI_STATE_BYTES);
  state[0] = MITSUBISHI_SIGNATURE[0];
  for (uint16_t bit = 0; bit < MITSUBISHI_SUFFIX_BYTES * 8U; bit++) {
    const uint16_t mark_index = start + bit * 2U;
    const uint16_t space_index = mark_index + 1U;
    if (!pulse_matches(raw[mark_index], true, BIT_MARK_MIN_US,
                       BIT_MARK_MAX_US))
      return false;

    bool one = false;
    if (pulse_matches(raw[space_index], false, ZERO_SPACE_MIN_US,
                      ZERO_SPACE_MAX_US)) {
      one = false;
    } else if (pulse_matches(raw[space_index], false, ONE_SPACE_MIN_US,
                             ONE_SPACE_MAX_US)) {
      one = true;
    } else {
      return false;
    }
    if (one) {
      const uint16_t state_bit = bit + 8U;
      state[state_bit / 8U] |=
          static_cast<uint8_t>(1U << (state_bit % 8U));
    }
  }

  return pulse_matches(
      raw[start + MITSUBISHI_SUFFIX_DATA_PULSES - 1U], true,
      BIT_MARK_MIN_US, BIT_MARK_MAX_US);
}

static uint16_t measure_data_prefix(const int32_t *raw, uint16_t count,
                                    uint16_t start, int32_t *first_bad_mark,
                                    int32_t *first_bad_space) {
  if (raw == nullptr ||
      static_cast<uint32_t>(start) + MITSUBISHI_DATA_PULSES > count)
    return 0;

  for (uint16_t bit = 0; bit < MITSUBISHI_STATE_BYTES * 8U; bit++) {
    const uint16_t mark_index = start + bit * 2U;
    const uint16_t space_index = mark_index + 1U;
    const int32_t mark = raw[mark_index];
    const int32_t space = raw[space_index];
    if (!pulse_matches(mark, true, BIT_MARK_MIN_US, BIT_MARK_MAX_US) ||
        (!pulse_matches(space, false, ZERO_SPACE_MIN_US, ZERO_SPACE_MAX_US) &&
         !pulse_matches(space, false, ONE_SPACE_MIN_US, ONE_SPACE_MAX_US))) {
      if (first_bad_mark != nullptr)
        *first_bad_mark = mark;
      if (first_bad_space != nullptr)
        *first_bad_space = space;
      return bit;
    }
  }

  const int32_t footer = raw[start + MITSUBISHI_DATA_PULSES - 1U];
  if (!pulse_matches(footer, true, BIT_MARK_MIN_US, BIT_MARK_MAX_US)) {
    if (first_bad_mark != nullptr)
      *first_bad_mark = footer;
    if (first_bad_space != nullptr)
      *first_bad_space = 0;
  }
  return MITSUBISHI_STATE_BYTES * 8U;
}

static bool decode_frame(const int32_t *raw, uint16_t count, uint16_t start,
                         uint8_t state[MITSUBISHI_STATE_BYTES]) {
  if (raw == nullptr || state == nullptr ||
      static_cast<uint32_t>(start) + MITSUBISHI_FRAME_PULSES > count)
    return false;
  if (!pulse_matches(raw[start], true, HEADER_MARK_MIN_US,
                     HEADER_MARK_MAX_US) ||
      !pulse_matches(raw[start + 1], false, HEADER_SPACE_MIN_US,
                     HEADER_SPACE_MAX_US))
    return false;
  return decode_data(raw, count, start + 2, state);
}

static bool append_pulse(int32_t *raw, uint16_t capacity, uint16_t *count,
                         int32_t pulse) {
  if (*count >= capacity)
    return false;
  raw[(*count)++] = pulse;
  return true;
}

}  // namespace

bool validate_mitsubishi_state(
    const uint8_t state[MITSUBISHI_STATE_BYTES]) {
  return signature_matches(state) && checksum_matches(state);
}

bool encode_mitsubishi_state(
    const uint8_t state[MITSUBISHI_STATE_BYTES], int32_t *raw,
    uint16_t capacity, uint16_t *pulse_count) {
  if (!validate_mitsubishi_state(state) || raw == nullptr ||
      pulse_count == nullptr || capacity < MITSUBISHI_CANONICAL_PULSES)
    return false;

  uint16_t output = 0;
  for (uint8_t repeat = 0; repeat < 2; repeat++) {
    if (!append_pulse(raw, capacity, &output, CANONICAL_HEADER_MARK_US) ||
        !append_pulse(raw, capacity, &output, CANONICAL_HEADER_SPACE_US))
      return false;

    for (size_t byte = 0; byte < MITSUBISHI_STATE_BYTES; byte++) {
      for (uint8_t bit = 0; bit < 8; bit++) {
        const bool one = (state[byte] & static_cast<uint8_t>(1U << bit)) != 0;
        if (!append_pulse(raw, capacity, &output,
                          CANONICAL_BIT_MARK_US) ||
            !append_pulse(raw, capacity, &output,
                          one ? CANONICAL_ONE_SPACE_US
                              : CANONICAL_ZERO_SPACE_US))
          return false;
      }
    }

    if (!append_pulse(raw, capacity, &output, CANONICAL_FOOTER_MARK_US) ||
        !append_pulse(raw, capacity, &output, CANONICAL_REPEAT_GAP_US))
      return false;
  }

  if (output != MITSUBISHI_CANONICAL_PULSES)
    return false;
  *pulse_count = output;
  return true;
}

uint16_t mitsubishi_replay_pulse_count(const int32_t *raw,
                                       uint16_t pulse_count) {
  if (raw == nullptr || pulse_count != MITSUBISHI_CANONICAL_PULSES)
    return pulse_count;

  // Do not truncate an arbitrary 584-pulse RAW profile. Both complete frames,
  // including their trailing gaps, must be byte-for-byte identical.
  if (std::memcmp(raw, raw + MITSUBISHI_REPEAT_PULSES,
                  static_cast<size_t>(MITSUBISHI_REPEAT_PULSES) *
                      sizeof(int32_t)) != 0)
    return pulse_count;

  uint8_t first[MITSUBISHI_STATE_BYTES]{};
  uint8_t second[MITSUBISHI_STATE_BYTES]{};
  if (!decode_frame(raw, pulse_count, 0, first) ||
      !decode_frame(raw, pulse_count, MITSUBISHI_REPEAT_PULSES, second) ||
      !validate_mitsubishi_state(first) ||
      !validate_mitsubishi_state(second) ||
      std::memcmp(first, second, sizeof(first)) != 0)
    return pulse_count;

  return MITSUBISHI_REPEAT_PULSES;
}

bool decode_and_normalize_mitsubishi(
    int32_t *raw, uint16_t *pulse_count, uint16_t capacity,
    uint8_t state[MITSUBISHI_STATE_BYTES], MitsubishiDecodeStats *stats) {
  if (raw == nullptr || pulse_count == nullptr || state == nullptr ||
      stats == nullptr || *pulse_count == 0 || *pulse_count > capacity)
    return false;

  *stats = {};
  stats->input_pulses = *pulse_count;
  for (uint16_t index = 0; index < *pulse_count; index++) {
    if (duration_abs(raw[index]) < 250)
      stats->input_short_pulses++;
  }

  const uint16_t cleaned = sanitize_glitches(raw, *pulse_count, stats);
  stats->cleaned_pulses = cleaned;
  if (cleaned < MITSUBISHI_SUFFIX_DATA_PULSES)
    return false;

  uint8_t first_valid[MITSUBISHI_STATE_BYTES]{};
  const auto accept_decoded =
      [&](const uint8_t decoded[MITSUBISHI_STATE_BYTES],
          bool recovered) -> bool {
    if (!signature_matches(decoded)) {
      stats->signature_failures++;
      return false;
    }
    if (!checksum_matches(decoded)) {
      stats->checksum_failures++;
      return false;
    }
    if (stats->valid_frames == 0) {
      std::memcpy(first_valid, decoded, sizeof(first_valid));
    } else if (std::memcmp(first_valid, decoded, sizeof(first_valid)) != 0) {
      stats->ambiguous_valid_frames = true;
      return false;
    }
    stats->valid_frames++;
    if (recovered)
      stats->recovered_frames++;
    return true;
  };

  for (uint16_t start = 0;
       static_cast<uint32_t>(start) + MITSUBISHI_FRAME_PULSES <= cleaned;
       start++) {
    if (!pulse_matches(raw[start], true, HEADER_MARK_MIN_US,
                       HEADER_MARK_MAX_US) ||
        !pulse_matches(raw[start + 1], false, HEADER_SPACE_MIN_US,
                       HEADER_SPACE_MAX_US))
      continue;
    stats->header_candidates++;

    uint8_t decoded[MITSUBISHI_STATE_BYTES]{};
    if (!decode_frame(raw, cleaned, start, decoded))
      continue;
    if (!accept_decoded(decoded, false) &&
        stats->ambiguous_valid_frames)
      return false;
  }

  // The StickS3 demodulating receiver can split a long leader mark with
  // interference even when the following 144 data bits remain intact. If no
  // header-qualified frame survived, scan possible data starts. This is not a
  // generic RAW fallback: every one of the 144 bits, the exact five-byte
  // device signature, the checksum, and the footer mark still have to pass.
  if (stats->valid_frames == 0) {
    for (uint16_t start = 0;
         static_cast<uint32_t>(start) + MITSUBISHI_DATA_PULSES <= cleaned;
         start++) {
      int32_t first_bad_mark = 0;
      int32_t first_bad_space = 0;
      const uint16_t prefix =
          measure_data_prefix(raw, cleaned, start, &first_bad_mark,
                              &first_bad_space);
      if (prefix > stats->longest_data_prefix_bits) {
        stats->longest_data_prefix_bits = prefix;
        stats->longest_data_prefix_start = start;
        stats->first_bad_mark = first_bad_mark;
        stats->first_bad_space = first_bad_space;
      }

      if (start >= 2 &&
          pulse_matches(raw[start - 2], true, HEADER_MARK_MIN_US,
                        HEADER_MARK_MAX_US) &&
          pulse_matches(raw[start - 1], false, HEADER_SPACE_MIN_US,
                        HEADER_SPACE_MAX_US))
        continue;

      uint8_t decoded[MITSUBISHI_STATE_BYTES]{};
      if (!decode_data(raw, cleaned, start, decoded))
        continue;
      stats->headerless_candidates++;
      if (!accept_decoded(decoded, true) &&
          stats->ambiguous_valid_frames)
        return false;
    }
  }

  // A distorted leader can absorb the first data byte. Since that byte is the
  // fixed 0x23 portion of this project's exact device signature, recover only
  // when the remaining 17 bytes form an uninterrupted 136-bit section with a
  // footer, match CB 26 01 01, and validate the full-state checksum after 0x23
  // is restored. No variable state byte is inferred.
  if (stats->valid_frames == 0) {
    for (uint16_t start = 0;
         static_cast<uint32_t>(start) +
                 MITSUBISHI_SUFFIX_DATA_PULSES <=
             cleaned;
         start++) {
      uint8_t decoded[MITSUBISHI_STATE_BYTES]{};
      if (!decode_suffix_without_first_signature_byte(
              raw, cleaned, start, decoded))
        continue;
      stats->suffix_candidates++;
      if (accept_decoded(decoded, true)) {
        stats->leading_signature_byte_recoveries++;
      } else if (stats->ambiguous_valid_frames) {
        return false;
      }
    }
  }

  if (stats->valid_frames == 0)
    return false;

  std::memcpy(state, first_valid, MITSUBISHI_STATE_BYTES);
  if (!encode_mitsubishi_state(state, raw, capacity, pulse_count))
    return false;

  uint8_t verified_first[MITSUBISHI_STATE_BYTES]{};
  uint8_t verified_second[MITSUBISHI_STATE_BYTES]{};
  if (!decode_frame(raw, *pulse_count, 0, verified_first) ||
      !decode_frame(raw, *pulse_count, MITSUBISHI_REPEAT_PULSES,
                    verified_second) ||
      std::memcmp(state, verified_first, MITSUBISHI_STATE_BYTES) != 0 ||
      std::memcmp(state, verified_second, MITSUBISHI_STATE_BYTES) != 0 ||
      !validate_mitsubishi_state(verified_first) ||
      !validate_mitsubishi_state(verified_second))
    return false;

  return true;
}

}  // namespace esphome::irvault
