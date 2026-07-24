#pragma once

namespace esphome::irvault {

/** Initializes and exposes the verified StickS3 board-level hardware. */
class StickS3Hardware {
 public:
  bool setup();
  bool prepare_ir_receive();
  int battery_level() const;
};

}  // namespace esphome::irvault
