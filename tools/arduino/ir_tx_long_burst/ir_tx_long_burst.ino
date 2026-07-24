#include "M5Unified.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

#define IR_SEND_PIN 46
#define IR_CARRIER_FREQ_HZ 38000
#define IR_DUTY_CYCLE 0.33

rmt_channel_handle_t tx_chan = NULL;
rmt_encoder_handle_t copy_encoder = NULL;

void setup_rmt_tx() {
  rmt_tx_channel_config_t tx_config = {
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
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_config, &tx_chan));

  rmt_carrier_config_t carrier_config = {
      .frequency_hz = IR_CARRIER_FREQ_HZ,
      .duty_cycle = IR_DUTY_CYCLE,
      .flags =
          {
              .polarity_active_low = false,
          },
  };
  ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &carrier_config));

  rmt_copy_encoder_config_t encoder_config = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoder_config, &copy_encoder));
  ESP_ERROR_CHECK(rmt_enable(tx_chan));
}

bool send_long_burst() {
  // RMT symbol durations are limited to 15 bits. Eight 60 ms symbols plus one
  // 20 ms half-symbol produce a continuous 500 ms carrier-gated high level.
  rmt_symbol_word_t symbols[9] = {};
  for (size_t i = 0; i < 8; i++) {
    symbols[i].duration0 = 30000;
    symbols[i].level0 = 1;
    symbols[i].duration1 = 30000;
    symbols[i].level1 = 1;
  }
  symbols[8].duration0 = 20000;
  symbols[8].level0 = 1;
  symbols[8].duration1 = 0;
  symbols[8].level1 = 0;

  rmt_transmit_config_t transmit_config = {
      .loop_count = 0,
      .flags =
          {
              .eot_level = 0,
          },
  };

  esp_err_t result =
      rmt_transmit(tx_chan, copy_encoder, symbols, sizeof(symbols),
                   &transmit_config);
  if (result == ESP_OK) {
    result = rmt_tx_wait_all_done(tx_chan, 2000);
  }
  return result == ESP_OK;
}

void setup() {
  M5.begin();
  Serial.begin(115200);

  M5.Display.setRotation(3);
  M5.Display.setTextFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("IR TX LONG BURST");
  M5.Display.println("GPIO46 38kHz 33%");
  M5.Display.println("500ms every 2sec");

  M5.Power.setExtOutput(true, m5::ext_none);
  delay(100);
  setup_rmt_tx();

  Serial.println("IR TX long-burst diagnostic ready");
  Serial.println("GPIO46, 38kHz, 33% duty, 500ms burst every 2 seconds");
}

void loop() {
  static uint32_t burst_count = 0;
  burst_count++;

  M5.Display.fillRect(0, 90, 240, 45, TFT_BLACK);
  M5.Display.setCursor(0, 90);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.printf("BURST %lu", (unsigned long)burst_count);

  const bool ok = send_long_burst();
  Serial.printf("BURST %lu: %s\n", (unsigned long)burst_count,
                ok ? "RMT complete" : "RMT ERROR");

  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  delay(1500);
}
