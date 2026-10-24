#include <driver/i2s.h>
#include <opus.h>
#include <cstring>
#include "main.h"
#include <esp_event.h>
#include <esp_log.h>
#include <stddef.h>
#include <stdint.h>

// Pre-recorded audio array declared in audio_data.cpp
extern const uint8_t audio[];
extern const size_t audio_len;

#define OPUS_OUT_BUFFER_SIZE 1276  // Recommended by libopus
#define SAMPLE_RATE 8000
#define BUFFER_SAMPLES 320         // 160 samples (20ms at 8kHz 16-bit mono)

#define MCLK_PIN 0
#define DAC_BCLK_PIN 15
#define DAC_LRCLK_PIN 16
#define DAC_DATA_PIN 17
#define ADC_BCLK_PIN 38
#define ADC_LRCLK_PIN 39
#define ADC_DATA_PIN 40

#define OPUS_ENCODER_BITRATE 30000
#define OPUS_ENCODER_COMPLEXITY 0

void oai_init_audio_capture() {
  i2s_config_t i2s_config_out = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = BUFFER_SAMPLES,
      .use_apll = 1,
      .tx_desc_auto_clear = true,
  };
  if (i2s_driver_install(I2S_NUM_0, &i2s_config_out, 0, NULL) != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to configure I2S driver for audio output");
    return;
  }

  i2s_pin_config_t pin_config_out = {
      .mck_io_num = MCLK_PIN,
      .bck_io_num = DAC_BCLK_PIN,
      .ws_io_num = DAC_LRCLK_PIN,
      .data_out_num = DAC_DATA_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };
  if (i2s_set_pin(I2S_NUM_0, &pin_config_out) != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to set I2S pins for audio output");
    return;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);

  i2s_config_t i2s_config_in = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = BUFFER_SAMPLES,
      .use_apll = 1,
  };
  if (i2s_driver_install(I2S_NUM_1, &i2s_config_in, 0, NULL) != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to configure I2S driver for audio input");
    return;
  }

  i2s_pin_config_t pin_config_in = {
      .mck_io_num = MCLK_PIN,
      .bck_io_num = ADC_BCLK_PIN,
      .ws_io_num = ADC_LRCLK_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = ADC_DATA_PIN,
  };
  if (i2s_set_pin(I2S_NUM_1, &pin_config_in) != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to set I2S pins for audio input");
    return;
  }
}

opus_int16 *output_buffer = NULL;
OpusDecoder *opus_decoder = NULL;

void oai_init_audio_decoder() {
  int decoder_error = 0;
  // Stereo decoder (2 channels)
  opus_decoder = opus_decoder_create(SAMPLE_RATE, 2, &decoder_error);
  if (decoder_error != OPUS_OK) {
    ESP_LOGE(LOG_TAG, "Failed to create OPUS decoder: %d", decoder_error);
    return;
  }

  // Allocate space for BUFFER_SAMPLES stereo frames (2 channels * BUFFER_SAMPLES shorts)
  output_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES * 2 * sizeof(opus_int16));
}

void oai_audio_decode(uint8_t *data, size_t size) {
  if (!opus_decoder || !output_buffer) return;

  int decoded_samples_per_channel =
      opus_decode(opus_decoder, data, size, output_buffer, BUFFER_SAMPLES, 0);

  if (decoded_samples_per_channel > 0) {
    size_t bytes_written = 0;
    // 2 channels * 16-bit (2 bytes) = 4 bytes per stereo sample frame
    size_t bytes_to_write = decoded_samples_per_channel * 2 * sizeof(opus_int16);
    i2s_write(I2S_NUM_0, output_buffer, bytes_to_write,
              &bytes_written, portMAX_DELAY);
  }
}

OpusEncoder *opus_encoder = NULL;
opus_int16 *encoder_input_buffer = NULL;
uint8_t *encoder_output_buffer = NULL;

void oai_init_audio_encoder() {
  int encoder_error;
  opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP,
                                     &encoder_error);
  if (encoder_error != OPUS_OK) {
    ESP_LOGE(LOG_TAG, "Failed to create OPUS encoder: %d", encoder_error);
    return;
  }

  if (opus_encoder_init(opus_encoder, SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP) !=
      OPUS_OK) {
    ESP_LOGE(LOG_TAG, "Failed to initialize OPUS encoder");
    return;
  }

  opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
  opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
  opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  encoder_input_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES);
  encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
  ESP_LOGI(LOG_TAG, "Opus encoder initialized at %d Hz mono", SAMPLE_RATE);
}

void oai_send_audio(PeerConnection *peer_connection) {
  static size_t offset = 0;
  static int packet_count = 0;
  static bool finished_logged = false;

  if (offset >= audio_len) {
    if (!finished_logged) {
      ESP_LOGI(LOG_TAG, ">>> Finished streaming WAV audio (%d bytes, %d packets sent). Awaiting OpenAI response...",
               (int)offset, packet_count);
      finished_logged = true;
    }
    return;
  }

  const size_t chunk_size =
      (audio_len - offset < BUFFER_SAMPLES)
          ? audio_len - offset
          : BUFFER_SAMPLES;

  memset(encoder_input_buffer, 0, BUFFER_SAMPLES);
  memcpy(encoder_input_buffer, audio + offset, chunk_size);
  offset += chunk_size;
  packet_count++;

  if (packet_count % 50 == 1) {
    ESP_LOGI(LOG_TAG, "Streaming audio to OpenAI: packet #%d (progress: %d/%d bytes, %d%%)",
             packet_count, (int)offset, (int)audio_len, (int)(offset * 100 / audio_len));
  }

  auto encoded_size =
      opus_encode(opus_encoder, encoder_input_buffer, BUFFER_SAMPLES / 2,
                  encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);

  if (encoded_size > 0) {
    peer_connection_send_audio(peer_connection, encoder_output_buffer,
                               encoded_size);
  }
}
