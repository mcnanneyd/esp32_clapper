#include "driver/i2s.h"
#include <Arduino.h>

// Acquisition params
#define SAMPLE_RATE        16000 // I2S
#define PRE_TRIGGER_MS     100   
#define POST_TRIGGER_MS    300
#define TRIGGER_THRESHOLD  20000

#define PRE_TRIGGER_SAMPLES  (SAMPLE_RATE * PRE_TRIGGER_MS / 1000)
#define POST_TRIGGER_SAMPLES (SAMPLE_RATE * POST_TRIGGER_MS / 1000)
#define TOTAL_SAMPLES (PRE_TRIGGER_SAMPLES + POST_TRIGGER_SAMPLES)


// INMP441 I2S PINS (Elegoo ESP32)
#define I2S_WS   15
#define I2S_SCK  14
#define I2S_SD   32

int16_t pre_buffer[PRE_TRIGGER_SAMPLES];
int16_t capture_buffer[TOTAL_SAMPLES];

volatile size_t pre_index = 0;

void setup_i2s() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

void setup() {
  Serial.begin(921600);
  setup_i2s();
}

void loop() {
  int16_t sample;
  size_t bytes_read;

  i2s_read(I2S_NUM_0, &sample, sizeof(sample), &bytes_read, portMAX_DELAY);

  // Circular pre-trigger buffer
  pre_buffer[pre_index++] = sample;
  if (pre_index >= PRE_TRIGGER_SAMPLES) pre_index = 0;

  // Trigger detection
  if (abs(sample) > TRIGGER_THRESHOLD) {

    // Copy pre-trigger data
    size_t idx = pre_index;
    for (size_t i = 0; i < PRE_TRIGGER_SAMPLES; i++) {
      // Copy from circular buffer
      capture_buffer[i] = pre_buffer[idx++];

      // Wrapping
      if (idx >= PRE_TRIGGER_SAMPLES) {
        idx = 0;
      }
    }

    // Capture post-triggr data
    for (size_t i = 0; i < POST_TRIGGER_SAMPLES; i++) {
      i2s_read(I2S_NUM_0,
               &capture_buffer[PRE_TRIGGER_SAMPLES + i],
               sizeof(int16_t),
               &bytes_read,
               portMAX_DELAY);
    }

    // Dump buffer to serial
    Serial.write((uint8_t*)"DUMP", 4);
    uint32_t count = TOTAL_SAMPLES;
    Serial.write((uint8_t*)&count, sizeof(count));
    Serial.write((uint8_t*)capture_buffer, sizeof(capture_buffer));

    delay(1000);
  }
}
