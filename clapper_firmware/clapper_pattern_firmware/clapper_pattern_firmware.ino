#include <Arduino.h>
#include "driver/i2s.h"
#define SAMPLE_RATE 16000

#define RMS_WINDOW_MS 10
#define NOISE_EMA_ALPHA 0.995f
#define RMS_TRIGGER_MULTIPLIER 6.5f
#define ZCR_MIN 2
#define ZCR_MAX 80

// Pattern detection parameters
#define SHORT_DOUBLE_CLAP_MIN_GAP_MS 80
#define SHORT_DOUBLE_CLAP_MAX_GAP_MS 300
#define LONG_DOUBLE_CLAP_MIN_GAP_MS 300
#define LONG_DOUBLE_CLAP_MAX_GAP_MS 600
#define PATTERN_TIMEOUT_MS 700  // Max time to wait after last clap
#define MAX_CLAPS_IN_PATTERN 10

#define ALWAYS_PRINT false
#define STATS_INTERVAL_MS 1000

// INMP441 I2S PINS (Elegoo ESP32)
#define I2S_WS   15
#define I2S_SCK  14
#define I2S_SD   32

#define RMS_WINDOW_SAMPLES (SAMPLE_RATE * RMS_WINDOW_MS / 1000)
#define HALF_WINDOW_SAMPLES (RMS_WINDOW_SAMPLES / 2)

int16_t rms_window[RMS_WINDOW_SAMPLES];
size_t rms_index = 0;

float noise_floor_rms = 0.0f;
bool noise_floor_initialized = false;

// Statistics tracking
unsigned long last_stats_time = 0;
float rms_sum = 0.0f;
float rms_sq_sum = 0.0f;
float zcr_sum = 0.0f;
float zcr_sq_sum = 0.0f;
float rms_min = 999999.0f;
float rms_max = 0.0f;
int zcr_min = 999999;
int zcr_max = 0;
int stats_count = 0;

// Pattern detection state
struct ClapEvent {
  unsigned long timestamp;
  float rms;
  int zcr;
};

ClapEvent clap_buffer[MAX_CLAPS_IN_PATTERN];
int clap_count = 0;
unsigned long last_clap_time = 0;
bool waiting_for_pattern = false;

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

void analyze_and_report_pattern() {
  if (clap_count < 2) {
    clap_count = 0;
    waiting_for_pattern = false;
    return;
  }

  unsigned long gaps[MAX_CLAPS_IN_PATTERN - 1];
  for (int i = 1; i < clap_count; i++) {
    gaps[i - 1] = clap_buffer[i].timestamp - clap_buffer[i - 1].timestamp;
  }

  // Calculate statistics
  float rms_avg = 0.0f;
  float rms_min_val = clap_buffer[0].rms;
  float rms_max_val = clap_buffer[0].rms;
  int zcr_avg = 0;
  
  for (int i = 0; i < clap_count; i++) {
    rms_avg += clap_buffer[i].rms;
    zcr_avg += clap_buffer[i].zcr;
    if (clap_buffer[i].rms < rms_min_val) rms_min_val = clap_buffer[i].rms;
    if (clap_buffer[i].rms > rms_max_val) rms_max_val = clap_buffer[i].rms;
  }
  rms_avg /= clap_count;
  zcr_avg /= clap_count;

  unsigned long total_duration = clap_buffer[clap_count - 1].timestamp - clap_buffer[0].timestamp;

  // Determine pattern type
  bool is_short_double = false;
  bool is_long_double = false;
  
  if (clap_count >= 2) {
    // Check if all gaps fit short double pattern
    is_short_double = true;
    for (int i = 0; i < clap_count - 1; i++) {
      if (gaps[i] < SHORT_DOUBLE_CLAP_MIN_GAP_MS || gaps[i] > SHORT_DOUBLE_CLAP_MAX_GAP_MS) {
        is_short_double = false;
        break;
      }
    }
    
    // Check if all gaps fit long double pattern
    is_long_double = true;
    for (int i = 0; i < clap_count - 1; i++) {
      if (gaps[i] < LONG_DOUBLE_CLAP_MIN_GAP_MS || gaps[i] > LONG_DOUBLE_CLAP_MAX_GAP_MS) {
        is_long_double = false;
        break;
      }
    }
  }

  // Report pattern
  Serial.print("PATTERN,");
  Serial.print(clap_count);           Serial.print(",");
  Serial.print(is_short_double ? "SHORT" : (is_long_double ? "LONG" : "MIXED"));  Serial.print(",");
  Serial.print(total_duration);       Serial.print(",");
  Serial.print(rms_avg, 2);           Serial.print(",");
  Serial.print(rms_min_val, 2);       Serial.print(",");
  Serial.print(rms_max_val, 2);       Serial.print(",");
  Serial.print(zcr_avg);              Serial.print(",");
  
  // Print individual gaps
  for (int i = 0; i < clap_count - 1; i++) {
    Serial.print(gaps[i]);
    if (i < clap_count - 2) Serial.print(":");
  }
  Serial.println();

  // Reset for next pattern
  clap_count = 0;
  waiting_for_pattern = false;
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
  
  if (rms < rms_min) rms_min = rms;
  if (rms > rms_max) rms_max = rms;
  if (zcr < zcr_min) zcr_min = zcr;
  if (zcr > zcr_max) zcr_max = zcr;
  
  stats_count++;

  // Initialize noise floor
  if (!noise_floor_initialized) {
    noise_floor_rms = rms;
    noise_floor_initialized = true;
  }

  // Check for trigger
  bool triggered =
    (rms > noise_floor_rms * RMS_TRIGGER_MULTIPLIER) &&
    (zcr > ZCR_MIN) && (zcr < ZCR_MAX);

  // Update noise floor only when quiet
  if (!triggered && rms < noise_floor_rms * 2.0f) {
    noise_floor_rms =
      NOISE_EMA_ALPHA * noise_floor_rms + (1.0f - NOISE_EMA_ALPHA) * rms;
  }

  // Handle pattern detection
  unsigned long current_time = millis();
  
  if (triggered) {
    // Check if this is a new clap (not same clap)
    if (!waiting_for_pattern || (current_time - last_clap_time > 50)) {
      // Add to pattern buffer
      if (clap_count < MAX_CLAPS_IN_PATTERN) {
        clap_buffer[clap_count].timestamp = current_time;
        clap_buffer[clap_count].rms = rms;
        clap_buffer[clap_count].zcr = zcr;
        clap_count++;
        last_clap_time = current_time;
        waiting_for_pattern = true;
      }
    }
  }

  // Check for pattern timeout
  if (waiting_for_pattern && (current_time - last_clap_time > PATTERN_TIMEOUT_MS)) {
    analyze_and_report_pattern();
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

  if (rms_index == HALF_WINDOW_SAMPLES || rms_index >= RMS_WINDOW_SAMPLES) {
    process_window();
    
    if (rms_index >= RMS_WINDOW_SAMPLES) {
      rms_index = 0;
    }
  }

  unsigned long current_time = millis();
  if (current_time - last_stats_time >= STATS_INTERVAL_MS) {
    send_statistics();
    last_stats_time = current_time;
  }
}
