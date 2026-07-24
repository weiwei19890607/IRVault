#include "M5Unified.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#define IR_SEND_PIN 46

uint16_t address = 0x0000;
uint8_t command = 0x55;
uint8_t repeats = 0;

rmt_channel_handle_t tx_chan = NULL;
rmt_encoder_handle_t copy_encoder = NULL;

#define NEC_HEADER_MARK 9000
#define NEC_HEADER_SPACE 4500
#define NEC_BIT_MARK 560
#define NEC_BIT_0_SPACE 560
#define NEC_BIT_1_SPACE 1690
#define NEC_REPEAT_MARK 9000
#define NEC_REPEAT_SPACE 2250

#define IR_CARRIER_FREQ_HZ 38000
#define IR_DUTY_CYCLE 0.33

void setup_rmt_tx();
bool sendNEC(uint16_t address, uint8_t command, uint8_t repeats);
void encodeNEC(uint32_t raw_data, rmt_symbol_word_t *symbols,
               size_t *symbol_count);
uint32_t NECRaw(uint16_t address, uint8_t command);

void setup() {
  M5.begin();
  Serial.begin(115200);

  M5.Display.setRotation(3);
  M5.Display.setTextFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.printf("StickS3 IR example");

  Serial.println("StickS3 IR example");

  setup_rmt_tx();

  Serial.printf("IR Send Pin: %d\n", IR_SEND_PIN);
  M5.Power.setExtOutput(true, m5::ext_none);
  delay(100);
}

void loop() {
  uint32_t raw = NECRaw(address, command);

  Serial.printf("Send NEC: addr=0x%04X, cmd=0x%02X, raw=0x%08X\n",
                address, command, raw);

  sendNEC(address, command, repeats);
  M5.Display.fillRect(0, 30, 240, 105, TFT_BLACK);
  M5.Display.setCursor(0, 30);
  M5.Display.printf("Send NEC:\n");
  M5.Display.printf(" addr=0x%04X\n", address);
  M5.Display.printf(" cmd =0x%02X\n", command);
  M5.Display.printf(" raw =0x%08X\n", raw);

  address += 0x0001;
  command += 0x01;
  repeats = 0;

  delay(2000);
}

void setup_rmt_tx() {
  rmt_tx_channel_config_t tx_chan_config = {
      .gpio_num = (gpio_num_t)IR_SEND_PIN,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 1000000,
      .mem_block_symbols = 64,
      .trans_queue_depth = 4,
      .flags =
          {
              .invert_out = false,
              .with_dma = false,
          },
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));

  rmt_carrier_config_t carrier_cfg = {
      .frequency_hz = IR_CARRIER_FREQ_HZ,
      .duty_cycle = IR_DUTY_CYCLE,
      .flags =
          {
              .polarity_active_low = false,
          },
  };
  ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &carrier_cfg));

  rmt_copy_encoder_config_t encoder_config = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoder_config, &copy_encoder));

  ESP_ERROR_CHECK(rmt_enable(tx_chan));
}

bool sendNEC(uint16_t address, uint8_t command, uint8_t repeats) {
  uint32_t raw = NECRaw(address, command);
  rmt_symbol_word_t symbols[68];
  size_t symbol_count = 0;

  encodeNEC(raw, symbols, &symbol_count);

  rmt_transmit_config_t tx_config = {
      .loop_count = 0,
      .flags =
          {
              .eot_level = 0,
          },
  };

  esp_err_t ret =
      rmt_transmit(tx_chan, copy_encoder, symbols,
                   symbol_count * sizeof(rmt_symbol_word_t), &tx_config);

  if (ret == ESP_OK) {
    ret = rmt_tx_wait_all_done(tx_chan, 1000);
  }

  for (int i = 0; i < repeats; i++) {
    delay(108);
  }

  return (ret == ESP_OK);
}

void encodeNEC(uint32_t raw_data, rmt_symbol_word_t *symbols,
               size_t *symbol_count) {
  size_t idx = 0;

  symbols[idx].duration0 = NEC_HEADER_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = NEC_HEADER_SPACE;
  symbols[idx].level1 = 0;
  idx++;

  for (int i = 0; i < 32; i++) {
    symbols[idx].duration0 = NEC_BIT_MARK;
    symbols[idx].level0 = 1;
    if (raw_data & (1UL << i)) {
      symbols[idx].duration1 = NEC_BIT_1_SPACE;
    } else {
      symbols[idx].duration1 = NEC_BIT_0_SPACE;
    }
    symbols[idx].level1 = 0;
    idx++;
  }

  symbols[idx].duration0 = NEC_BIT_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = 0;
  symbols[idx].level1 = 0;
  idx++;

  *symbol_count = idx;
}

uint32_t NECRaw(uint16_t address, uint8_t command) {
  uint16_t nec_addr;

  if (address <= 0x00FF) {
    uint8_t addr8 = address & 0xFF;
    nec_addr = ((uint16_t)(~addr8) << 8) | addr8;
  } else {
    nec_addr = address;
  }

  uint32_t raw = 0;
  raw |= (uint32_t)nec_addr;
  raw |= (uint32_t)command << 16;
  raw |= (uint32_t)(~command) << 24;

  return raw;
}
