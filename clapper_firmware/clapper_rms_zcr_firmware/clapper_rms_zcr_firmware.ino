#include <Arduino.h>
#include "driver/i2s.h"
#define SAMPLE_RATE 16000

#define RMS_WINDOW_MS 10
#define NOISE_EMA_ALPHA 0.995f
#define RMS_TRIGGER_MULTIPLIER 6.5f
#define ZCR_MIN 2
#define ZCR_MAX 80

// TODO: Add rate limiting for serial (w/ averaging, stdev)
#define ALWAYS_PRINT false
#define STATS_INTERVAL_MS 1000
#define TRIGGER_COOLDOWN_MS 100  // Minimum time between trigger prints

// INMP441 I2S PINS (Elegoo ESP32)
#define I2S_WS   15
#define I2S_SCK  14
#define I2S_SD   32

#define RMS_WINDOW_SAMPLES (SAMPLE_RATE * RMS_WINDOW_MS / 1000)
#define HALF_WINDOW_SAMPLES (RMS_WINDOW_SAMPLES / 2)

int16_t rms_window[RMS_WINDOW_SAMPLES];
size_t rms_index = 0;

float noise_floor_rms = 0.0f;  // Start at 0, will initialize on first window
bool noise_floor_initialized = false;

// Statistics tracking
unsigned long last_stats_time = 0;
unsigned long last_trigger_time = 0;
float rms_sum = 0.0f;
float rms_sq_sum = 0.0f;
float zcr_sum = 0.0f;
float zcr_sq_sum = 0.0f;
float rms_min = 999999.0f;
float rms_max = 0.0f;
int zcr_min = 999999;
int zcr_max = 0;
int stats_count = 0;

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

void process_window() {
  int64_t sum_sq = 0;
  int zcr = 0;

  for (size_t i = 0; i < RMS_WINDOW_SAMPLES; i++) {
    int16_t s = rms_window[i];
    sum_sq += (int32_t)s * s;

    if (i > 0 && ((rms_window[i - 1] ^ s) < 0)) {
      zcr++;
    }
  }

  float rms = sqrt((float)sum_sq / RMS_WINDOW_SAMPLES);

  // Accumulate statistics
  rms_sum += rms;
  rms_sq_sum += rms * rms;
  zcr_sum += zcr;
  zcr_sq_sum += zcr * zcr;
  
  // Track min/max
  if (rms < rms_min) rms_min = rms;
  if (rms > rms_max) rms_max = rms;
  if (zcr < zcr_min) zcr_min = zcr;
  if (zcr > zcr_max) zcr_max = zcr;
  
  stats_count++;

  // Initialize noise floor on first window
  if (!noise_floor_initialized) {
    noise_floor_rms = rms;
    noise_floor_initialized = true;
  }

  // Only update noise floor EMA if it's relatively quiet AND not triggered
  // This prevents claps from raising the noise floor
  bool triggered =
    (rms > noise_floor_rms * RMS_TRIGGER_MULTIPLIER) &&
    (zcr > ZCR_MIN) && (zcr < ZCR_MAX);

  if (!triggered && rms < noise_floor_rms * 2.0f) {
    noise_floor_rms =
      NOISE_EMA_ALPHA * noise_floor_rms + (1.0f - NOISE_EMA_ALPHA) * rms;
  }

  if (ALWAYS_PRINT || triggered) {
    // Rate limit trigger prints
    unsigned long current_time = millis();
    if (!triggered || (current_time - last_trigger_time >= TRIGGER_COOLDOWN_MS)) {
      Serial.print(rms, 2);               Serial.print(",");
      Serial.print(zcr);                  Serial.print(",");
      Serial.print(noise_floor_rms, 2);   Serial.print(",");
      Serial.print(triggered ? 1 : 0);    Serial.print(",");
      Serial.println(current_time);
      
      if (triggered) {
        last_trigger_time = current_time;
      }
    }
  }
}

void send_statistics() {
  if (stats_count == 0) return;

  float rms_avg = rms_sum / stats_count;
  float rms_variance = (rms_sq_sum / stats_count) - (rms_avg * rms_avg);
  float rms_stdev = sqrt(rms_variance > 0 ? rms_variance : 0);

  float zcr_avg = zcr_sum / stats_count;
  float zcr_variance = (zcr_sq_sum / stats_count) - (zcr_avg * zcr_avg);
  float zcr_stdev = sqrt(zcr_variance > 0 ? zcr_variance : 0);

  float dynamic_threshold = noise_floor_rms * RMS_TRIGGER_MULTIPLIER;

  Serial.print("STATS,");
  Serial.print(rms_avg, 2);    Serial.print(",");
  Serial.print(rms_stdev, 2);  Serial.print(",");
  Serial.print(rms_min, 2);    Serial.print(",");
  Serial.print(rms_max, 2);    Serial.print(",");
  Serial.print(zcr_avg, 2);    Serial.print(",");
  Serial.print(zcr_stdev, 2);  Serial.print(",");
  Serial.print(zcr_min);       Serial.print(",");
  Serial.print(zcr_max);       Serial.print(",");
  Serial.print(noise_floor_rms, 2);  Serial.print(",");
  Serial.println(dynamic_threshold, 2);

  // Reset accumulators
  rms_sum = 0.0f;
  rms_sq_sum = 0.0f;
  zcr_sum = 0.0f;
  zcr_sq_sum = 0.0f;
  rms_min = 999999.0f;
  rms_max = 0.0f;
  zcr_min = 999999;
  zcr_max = 0;
  stats_count = 0;
}

void setup() {
  Serial.begin(921600);
  setup_i2s();
  last_stats_time = millis();
}

void loop() {
  int16_t sample;
  size_t bytes_read;

  i2s_read(I2S_NUM_0, &sample, sizeof(sample), &bytes_read, portMAX_DELAY);

  rms_window[rms_index++] = sample;

  // Process at half-window intervals (50% overlap)
  if (rms_index == HALF_WINDOW_SAMPLES || rms_index >= RMS_WINDOW_SAMPLES) {
    process_window();
    
    // Reset to beginning when we reach the end
    if (rms_index >= RMS_WINDOW_SAMPLES) {
      rms_index = 0;
    }
  }

  // Send statistics periodically
  unsigned long current_time = millis();
  if (current_time - last_stats_time >= STATS_INTERVAL_MS) {
    send_statistics();
    last_stats_time = current_time;
  }
}
