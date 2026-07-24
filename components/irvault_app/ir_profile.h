#pragma once

#include <cstdint>
#include <cstring>
#include <new>

#include "esp_heap_caps.h"

namespace esphome::irvault {

static constexpr uint16_t MAX_RAW_DURATIONS = 2048;
static constexpr uint32_t DEFAULT_CARRIER_HZ = 38000;
static constexpr uint32_t IR_PROFILE_MAGIC = 0x49525632;
static constexpr uint16_t IR_PROFILE_VERSION = 2;

/** Versioned, protocol-agnostic RAW infrared profile stored in one slot. */
struct IRProfile {
  uint32_t magic{IR_PROFILE_MAGIC};
  uint16_t version{IR_PROFILE_VERSION};
  uint16_t pulse_count{0};
  uint32_t generation{0};
  uint32_t carrier_hz{DEFAULT_CARRIER_HZ};
  char display_name[8]{};
  uint64_t created_timestamp{0};
  uint64_t last_used_timestamp{0};
  int32_t raw[MAX_RAW_DURATIONS]{};
  uint32_t crc{0};
};

/**
 * Allocate a long RAW profile in octal PSRAM. Internal RAM is retained as a
 * last-resort fallback so allocation failure remains diagnosable rather than
 * causing a null dereference.
 */
inline IRProfile *allocate_ir_profile() {
  void *memory =
      heap_caps_calloc(1, sizeof(IRProfile), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr)
    memory = heap_caps_calloc(1, sizeof(IRProfile), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (memory == nullptr)
    return nullptr;
  return new (memory) IRProfile{};
}

inline void reset_ir_profile(IRProfile *profile) {
  if (profile == nullptr)
    return;
  std::memset(profile, 0, sizeof(IRProfile));
  profile->magic = IR_PROFILE_MAGIC;
  profile->version = IR_PROFILE_VERSION;
  profile->carrier_hz = DEFAULT_CARRIER_HZ;
}

inline void copy_ir_profile(IRProfile *destination, const IRProfile &source) {
  if (destination != nullptr)
    std::memcpy(destination, &source, sizeof(IRProfile));
}

}  // namespace esphome::irvault
