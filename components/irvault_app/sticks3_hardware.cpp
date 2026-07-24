#include "sticks3_hardware.h"

#include <M5Unified.h>

#include "esphome/core/log.h"

namespace esphome::irvault {

static const char *const TAG = "irvault.hardware";

bool StickS3Hardware::setup() {
  auto config = M5.config();
  config.serial_baudrate = 0;
  // Match the official StickS3 IR lifecycle: initialize the speaker subsystem,
  // then end it before RMT reception so the amplifier cannot interfere.
  config.internal_spk = true;
  config.internal_mic = false;
  config.internal_imu = false;
  config.output_power = false;
  config.clear_display = true;
  M5.begin(config);

  const bool board_ok = M5.getBoard() == m5::board_t::board_M5StickS3;
  const bool pmic_ok = M5.Power.getType() == m5::Power_Class::pmic_m5pm1 &&
                       M5.Power.M5pm1.begin();
  ESP_LOGI(TAG, "[APP] StickS3=%s M5PM1=%s", YESNO(board_ok), YESNO(pmic_ok));
  return board_ok && pmic_ok && this->prepare_ir_receive();
}

bool StickS3Hardware::prepare_ir_receive() {
  // M5Unified's StickS3 speaker callback controls the amplifier through
  // M5PM1 GPIO3. End the speaker first, then explicitly reproduce and verify
  // the callback's disabled state so IR learning never relies on an
  // unverified one-shot write.
  M5.Speaker.end();
  auto &pmic = M5.Power.M5pm1;
  const bool function_ok =
      pmic.setGPIOFunction(m5::M5PM1_Class::gpio3,
                           m5::M5PM1_Class::gpio);
  const bool mode_ok =
      pmic.setGPIOMode(m5::M5PM1_Class::gpio3,
                       m5::M5PM1_Class::output);
  const bool drive_ok =
      pmic.setGPIODrive(m5::M5PM1_Class::gpio3,
                        m5::M5PM1_Class::push_pull);
  const bool level_write_ok =
      pmic.setGPIOOutput(m5::M5PM1_Class::gpio3, false);
  const bool speaker_latch_low =
      !pmic.getGPIOOutputLatch(m5::M5PM1_Class::gpio3);
  const bool ext_write_ok = pmic.setExtOutput(true);
  const bool ext_enabled = pmic.getExtOutput();
  const bool ok = function_ok && mode_ok && drive_ok && level_write_ok &&
                  speaker_latch_low && ext_write_ok && ext_enabled;
  ESP_LOGI(TAG,
           "[IR] PMIC verify speaker_gpio3=%s EXT_5V=%s "
           "writes=%s/%s/%s/%s/%s",
           speaker_latch_low ? "LOW(disabled)" : "HIGH(ERROR)",
           ext_enabled ? "ON" : "OFF(ERROR)", YESNO(function_ok),
           YESNO(mode_ok), YESNO(drive_ok), YESNO(level_write_ok),
           YESNO(ext_write_ok));
  return ok;
}

int StickS3Hardware::battery_level() const {
  const int level = M5.Power.getBatteryLevel();
  return level < 0 ? -1 : (level > 100 ? 100 : level);
}

}  // namespace esphome::irvault
