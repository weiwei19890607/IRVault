#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../components/irvault_app/mitsubishi_codec.h"
#include "../components/irvault_app/factory_defaults.h"

using esphome::irvault::FACTORY_DEFAULT_SLOT_COUNT;
using esphome::irvault::MITSUBISHI_CANONICAL_PULSES;
using esphome::irvault::MITSUBISHI_REPEAT_PULSES;
using esphome::irvault::MITSUBISHI_STATE_BYTES;
using esphome::irvault::MitsubishiDecodeStats;
using esphome::irvault::decode_and_normalize_mitsubishi;
using esphome::irvault::encode_mitsubishi_state;
using esphome::irvault::factory_default_mitsubishi_state;
using esphome::irvault::mitsubishi_replay_pulse_count;
using esphome::irvault::validate_mitsubishi_state;

static constexpr std::array<uint8_t, MITSUBISHI_STATE_BYTES> COOL_STATE = {
    0x23, 0xCB, 0x26, 0x01, 0x01, 0x24, 0x03, 0x05, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x0B,
};

static std::vector<int32_t> encode(
    const std::array<uint8_t, MITSUBISHI_STATE_BYTES> &state) {
  std::vector<int32_t> raw(2048);
  uint16_t count = 0;
  assert(encode_mitsubishi_state(state.data(), raw.data(), raw.size(), &count));
  assert(count == MITSUBISHI_CANONICAL_PULSES);
  raw.resize(count);
  return raw;
}

static void add_split_glitch(std::vector<int32_t> *raw, size_t index) {
  const int32_t original = raw->at(index);
  assert(original < -300);
  const int32_t first = original / 2;
  const int32_t second = original - first;
  raw->erase(raw->begin() + index);
  raw->insert(raw->begin() + index, {first, 100, second});
}

int main() {
  assert(validate_mitsubishi_state(COOL_STATE.data()));

  {
    const std::array<std::array<uint8_t, MITSUBISHI_STATE_BYTES>,
                     FACTORY_DEFAULT_SLOT_COUNT>
        expected = {{
            {0x23, 0xCB, 0x26, 0x01, 0x01, 0x24, 0x03, 0x05, 0x09,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x0B},
            {0x23, 0xCB, 0x26, 0x01, 0x01, 0x24, 0x01, 0x06, 0x09,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x0A},
            {0x23, 0xCB, 0x26, 0x01, 0x01, 0x20, 0x01, 0x06, 0x09,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x06},
        }};

    for (uint8_t slot = 0; slot < FACTORY_DEFAULT_SLOT_COUNT; slot++) {
      const uint8_t *state = factory_default_mitsubishi_state(slot);
      assert(state != nullptr);
      assert(std::equal(expected[slot].begin(), expected[slot].end(), state));
      assert(validate_mitsubishi_state(state));

      std::vector<int32_t> raw(MITSUBISHI_CANONICAL_PULSES);
      uint16_t pulse_count = 0;
      assert(encode_mitsubishi_state(
          state, raw.data(), raw.size(), &pulse_count));
      assert(pulse_count == MITSUBISHI_CANONICAL_PULSES);
      assert(mitsubishi_replay_pulse_count(raw.data(), pulse_count) ==
             MITSUBISHI_REPEAT_PULSES);
    }
    assert(factory_default_mitsubishi_state(
               FACTORY_DEFAULT_SLOT_COUNT) == nullptr);
  }

  {
    auto raw = encode(COOL_STATE);
    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = raw.size();
    assert(decode_and_normalize_mitsubishi(
        raw.data(), &count, raw.capacity(), decoded.data(), &stats));
    assert(decoded == COOL_STATE);
    assert(stats.valid_frames == 2);
    assert(count == MITSUBISHI_CANONICAL_PULSES);
  }

  {
    auto raw = encode(COOL_STATE);
    assert(mitsubishi_replay_pulse_count(raw.data(), raw.size()) ==
           MITSUBISHI_REPEAT_PULSES);

    // A single stored frame is already single-frame and remains unchanged.
    assert(mitsubishi_replay_pulse_count(raw.data(),
                                         MITSUBISHI_REPEAT_PULSES) ==
           MITSUBISHI_REPEAT_PULSES);

    // Do not truncate if the two stored frames differ.
    auto unequal = raw;
    unequal[MITSUBISHI_REPEAT_PULSES + 3] =
        unequal[MITSUBISHI_REPEAT_PULSES + 3] == 420 ? 1300 : 420;
    assert(mitsubishi_replay_pulse_count(unequal.data(), unequal.size()) ==
           MITSUBISHI_CANONICAL_PULSES);

    // Identical halves are insufficient: signature/checksum validation must
    // still pass before replay is shortened.
    auto invalid = raw;
    invalid[3] = invalid[3] == 420 ? 1300 : 420;
    std::copy(invalid.begin(),
              invalid.begin() + MITSUBISHI_REPEAT_PULSES,
              invalid.begin() + MITSUBISHI_REPEAT_PULSES);
    assert(mitsubishi_replay_pulse_count(invalid.data(), invalid.size()) ==
           MITSUBISHI_CANONICAL_PULSES);
  }

  {
    auto raw = encode(COOL_STATE);
    add_split_glitch(&raw, 20);
    add_split_glitch(&raw, MITSUBISHI_REPEAT_PULSES + 42);
    raw.insert(raw.begin(), 90);
    raw.push_back(-110);
    raw.resize(2048);

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = MITSUBISHI_CANONICAL_PULSES + 6;
    assert(decode_and_normalize_mitsubishi(
        raw.data(), &count, raw.size(), decoded.data(), &stats));
    assert(decoded == COOL_STATE);
    assert(stats.merged_glitches == 2);
    assert(stats.trimmed_edge_glitches == 2);
    assert(count == MITSUBISHI_CANONICAL_PULSES);
  }

  {
    auto first = encode(COOL_STATE);
    std::array<uint8_t, MITSUBISHI_STATE_BYTES> heat = COOL_STATE;
    heat[6] = 0x01;
    heat[17] = 0x09;
    auto second = encode(heat);
    std::copy(second.begin() + MITSUBISHI_REPEAT_PULSES, second.end(),
              first.begin() + MITSUBISHI_REPEAT_PULSES);

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = first.size();
    assert(!decode_and_normalize_mitsubishi(
        first.data(), &count, first.size(), decoded.data(), &stats));
    assert(stats.ambiguous_valid_frames);
  }

  {
    auto corrupted = encode(COOL_STATE);
    static constexpr size_t DATA_BIT = 6 * 8;
    corrupted[3 + DATA_BIT * 2] =
        corrupted[3 + DATA_BIT * 2] == 420 ? 1300 : 420;
    corrupted[MITSUBISHI_REPEAT_PULSES + 3 + DATA_BIT * 2] =
        corrupted[MITSUBISHI_REPEAT_PULSES + 3 + DATA_BIT * 2] == 420
            ? 1300
            : 420;

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = corrupted.size();
    assert(!decode_and_normalize_mitsubishi(
        corrupted.data(), &count, corrupted.size(), decoded.data(), &stats));
    assert(stats.valid_frames == 0);
    // The suffix-recovery pass may inspect additional checksum-invalid
    // alignments after both complete frames fail. None may be accepted.
    assert(stats.checksum_failures >= 2);
  }

  {
    auto canonical = encode(COOL_STATE);
    std::vector<int32_t> capture(2048, 0);
    const std::array<int32_t, 7> leading_noise = {
        -1200, 800, -400, 400, -700, 900, -500,
    };
    std::copy(leading_noise.begin(), leading_noise.end(), capture.begin());
    std::copy(canonical.begin(),
              canonical.begin() + MITSUBISHI_REPEAT_PULSES,
              capture.begin() + leading_noise.size());

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count =
        leading_noise.size() + MITSUBISHI_REPEAT_PULSES;
    assert(decode_and_normalize_mitsubishi(
        capture.data(), &count, capture.size(), decoded.data(), &stats));
    assert(decoded == COOL_STATE);
    assert(stats.valid_frames == 1);
    assert(count == MITSUBISHI_CANONICAL_PULSES);
  }

  {
    auto damaged_headers = encode(COOL_STATE);
    damaged_headers[0] = -1800;
    damaged_headers[1] = 800;
    damaged_headers[MITSUBISHI_REPEAT_PULSES] = -1900;
    damaged_headers[MITSUBISHI_REPEAT_PULSES + 1] = 780;

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = damaged_headers.size();
    assert(decode_and_normalize_mitsubishi(
        damaged_headers.data(), &count, damaged_headers.size(),
        decoded.data(), &stats));
    assert(decoded == COOL_STATE);
    assert(stats.header_candidates == 0);
    assert(stats.recovered_frames == 2);
    assert(count == MITSUBISHI_CANONICAL_PULSES);
  }

  {
    // Real StickS3 captures can contain shortened active-low marks while all
    // data-bearing spaces remain valid. These must not be merged away as
    // glitches.
    auto shortened_marks = encode(COOL_STATE);
    const std::array<size_t, 8> shortened_bits = {
        0, 6, 29, 46, 58, 65, 68, 127,
    };
    const std::array<int32_t, 8> measured_marks = {
        -119, -144, -145, -200, -175, -228, -230, -200,
    };
    shortened_marks[0] = -1250;
    shortened_marks[1] = 959;
    shortened_marks[MITSUBISHI_REPEAT_PULSES] = -1214;
    shortened_marks[MITSUBISHI_REPEAT_PULSES + 1] = 1979;
    for (size_t index = 0; index < shortened_bits.size(); index++) {
      shortened_marks[2 + shortened_bits[index] * 2] =
          measured_marks[index];
      shortened_marks[MITSUBISHI_REPEAT_PULSES + 2 +
                      shortened_bits[index] * 2] = measured_marks[index];
    }

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = shortened_marks.size();
    assert(decode_and_normalize_mitsubishi(
        shortened_marks.data(), &count, shortened_marks.size(),
        decoded.data(), &stats));
    assert(decoded == COOL_STATE);
    assert(stats.header_candidates == 0);
    assert(stats.recovered_frames == 2);
    const size_t expected_preserved =
        std::count_if(measured_marks.begin(), measured_marks.end(),
                      [](int32_t mark) { return mark >= -220; }) *
        2;
    assert(stats.preserved_short_marks == expected_preserved);
    assert(count == MITSUBISHI_CANONICAL_PULSES);
  }

  {
    // Pulses below the measured recovery floor in a variable state byte
    // remain glitches and must not create an accepted frame. (Damage confined
    // to the fixed first signature byte is covered by the separately guarded
    // suffix-recovery test below.)
    auto invalid_marks = encode(COOL_STATE);
    static constexpr size_t VARIABLE_BYTE_MARK = 2 + 6 * 8 * 2;
    invalid_marks[VARIABLE_BYTE_MARK] = -99;
    invalid_marks[MITSUBISHI_REPEAT_PULSES + VARIABLE_BYTE_MARK] = -99;

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = invalid_marks.size();
    assert(!decode_and_normalize_mitsubishi(
        invalid_marks.data(), &count, invalid_marks.size(),
        decoded.data(), &stats));
    assert(stats.valid_frames == 0);
  }

  {
    // A short positive glitch can split one legitimate mark into two pieces.
    // Positive glitches must be repaired before short negative marks are
    // considered, or adjacent data spaces will be merged and a bit is lost.
    auto split_marks = encode(COOL_STATE);
    const size_t first_mark = 2 + 12 * 2;
    const size_t second_mark =
        MITSUBISHI_REPEAT_PULSES + 2 + 12 * 2;
    split_marks.erase(split_marks.begin() + second_mark);
    split_marks.insert(split_marks.begin() + second_mark,
                       {-119, 154, -68});
    split_marks.erase(split_marks.begin() + first_mark);
    split_marks.insert(split_marks.begin() + first_mark,
                       {-119, 154, -68});
    split_marks.resize(2048);

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = MITSUBISHI_CANONICAL_PULSES + 4;
    assert(decode_and_normalize_mitsubishi(
        split_marks.data(), &count, split_marks.size(),
        decoded.data(), &stats));
    assert(decoded == COOL_STATE);
    assert(stats.merged_glitches >= 2);
    assert(count == MITSUBISHI_CANONICAL_PULSES);
  }

  {
    // If the damaged leader absorbs only the fixed first signature byte 0x23,
    // the remaining 17 bytes can be recovered without inferring any variable
    // state. Exact suffix signature and checksum validation are still required.
    auto canonical = encode(COOL_STATE);
    std::vector<int32_t> suffix_only(2048, 0);
    static constexpr size_t FIRST_BYTE_PULSES = 8 * 2;
    const size_t suffix_start = 2 + FIRST_BYTE_PULSES;
    const size_t suffix_end = MITSUBISHI_REPEAT_PULSES - 1;
    std::copy(canonical.begin() + suffix_start,
              canonical.begin() + suffix_end, suffix_only.begin());

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    uint16_t count = suffix_end - suffix_start;
    assert(count == (MITSUBISHI_STATE_BYTES - 1) * 8 * 2 + 1);
    assert(decode_and_normalize_mitsubishi(
        suffix_only.data(), &count, suffix_only.size(),
        decoded.data(), &stats));
    assert(decoded == COOL_STATE);
    assert(stats.leading_signature_byte_recoveries == 1);
    assert(count == MITSUBISHI_CANONICAL_PULSES);
  }

  {
    auto other_signature = COOL_STATE;
    other_signature[4] = 0x00;
    other_signature[17] = 0;
    for (size_t index = 0; index < MITSUBISHI_STATE_BYTES - 1; index++)
      other_signature[17] =
          static_cast<uint8_t>(other_signature[17] + other_signature[index]);

    std::vector<int32_t> raw(2048);
    uint16_t count = 0;
    // The production encoder intentionally refuses unsupported signatures, so
    // start from a valid frame and flip the signature and checksum bits.
    raw = encode(COOL_STATE);
    for (uint8_t bit = 0; bit < 8; bit++) {
      const bool wanted = (other_signature[4] & (1U << bit)) != 0;
      raw[3 + (4 * 8 + bit) * 2] = wanted ? 1300 : 420;
      raw[MITSUBISHI_REPEAT_PULSES + 3 + (4 * 8 + bit) * 2] =
          wanted ? 1300 : 420;
      const bool checksum_bit =
          (other_signature[17] & (1U << bit)) != 0;
      raw[3 + (17 * 8 + bit) * 2] = checksum_bit ? 1300 : 420;
      raw[MITSUBISHI_REPEAT_PULSES + 3 + (17 * 8 + bit) * 2] =
          checksum_bit ? 1300 : 420;
    }
    count = raw.size();

    std::array<uint8_t, MITSUBISHI_STATE_BYTES> decoded{};
    MitsubishiDecodeStats stats{};
    assert(!decode_and_normalize_mitsubishi(
        raw.data(), &count, raw.size(), decoded.data(), &stats));
    // The suffix-recovery pass may inspect additional signature-invalid
    // alignments after both complete frames fail. None may be accepted.
    assert(stats.signature_failures >= 2);
    assert(stats.checksum_failures == 0);
  }

  return 0;
}
