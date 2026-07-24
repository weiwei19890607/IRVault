#pragma once

#include <cstdint>

#include "mitsubishi_codec.h"

namespace esphome::irvault {

static constexpr uint8_t FACTORY_DEFAULT_SLOT_COUNT = 3;

/**
 * Return the checksum-validated Mitsubishi state shipped for a slot.
 *
 * Slot order is Cool, Heat, Off. The returned storage has static lifetime.
 */
const uint8_t *factory_default_mitsubishi_state(uint8_t slot);

}  // namespace esphome::irvault
