#include "factory_defaults.h"

namespace esphome::irvault {

namespace {

// Extracted from the newest active V2 records on the verified StickS3.
//
// The active Cool record contained an incomplete leading fragment followed by
// this checksum-valid state. Factory initialization deliberately stores the
// clean canonical encoding of that state rather than preserving receiver
// noise. Heat and Off were already canonical dual-frame records.
static constexpr uint8_t FACTORY_STATES
    [FACTORY_DEFAULT_SLOT_COUNT][MITSUBISHI_STATE_BYTES] = {
        // Cool
        {0x23, 0xCB, 0x26, 0x01, 0x01, 0x24, 0x03, 0x05, 0x09,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x0B},
        // Heat
        {0x23, 0xCB, 0x26, 0x01, 0x01, 0x24, 0x01, 0x06, 0x09,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x0A},
        // Off
        {0x23, 0xCB, 0x26, 0x01, 0x01, 0x20, 0x01, 0x06, 0x09,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x06},
};

}  // namespace

const uint8_t *factory_default_mitsubishi_state(uint8_t slot) {
  return slot < FACTORY_DEFAULT_SLOT_COUNT ? FACTORY_STATES[slot] : nullptr;
}

}  // namespace esphome::irvault
