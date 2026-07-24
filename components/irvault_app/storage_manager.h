#pragma once

#include <cstddef>

#include "nvs.h"
#include "ir_profile.h"

namespace esphome::irvault {

/**
 * Stores variable-length RAW records as two CRC-verified NVS generations.
 *
 * Only pulse_count durations are persisted. Long working buffers live in
 * PSRAM, while per-slot RAM state is limited to generation/copy metadata.
 */
class StorageManager {
 public:
  bool setup();
  bool is_learned(uint8_t slot) const;
  bool save_slot(uint8_t slot, const IRProfile &profile);
  bool load_slot(uint8_t slot, IRProfile *profile);

 protected:
  bool read_record_(uint8_t slot, uint8_t copy, IRProfile *profile,
                    uint32_t *generation);
  bool write_record_(uint8_t slot, uint8_t copy, const IRProfile &profile,
                     uint32_t generation);
  bool migrate_legacy_slot_(uint8_t slot);
  bool seed_factory_slot_(uint8_t slot);
  static const char *key_(uint8_t slot, uint8_t copy);

  nvs_handle_t nvs_handle_{0};
  uint8_t active_copy_[3]{255, 255, 255};
  uint32_t generation_[3]{0, 0, 0};
  bool learned_[3]{false, false, false};
  IRProfile *scratch_{nullptr};
  uint8_t *record_buffer_{nullptr};
  size_t record_capacity_{0};
};

}  // namespace esphome::irvault
