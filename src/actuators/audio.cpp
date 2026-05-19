// =============================================================================
// audio.cpp — Reproductor I2S para MAX98357.
// Configura I2S_NUM_0 en modo master TX, 16-bit, mono (sólo canal izquierdo)
// y reproduce notas generando senoides por nota a partir de song_data.h.
// =============================================================================
#include "audio.h"
#include "../core/config.h"
#include "song_data.h"
#include "driver/i2s.h"
#include <math.h>

static const i2s_port_t I2S_PORT = (i2s_port_t)I2S_PORT_NUM;
static volatile bool s_stopRequested = false;

// =============================================================================
// audio_init — configura el driver I2S y los pines.
// =============================================================================
bool audio_init() {
  i2s_config_t cfg = {};
  cfg.mode              = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate       = I2S_SAMPLE_RATE;
  cfg.bits_per_sample   = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format    = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags  = 0;
  cfg.dma_buf_count     = 8;
  cfg.dma_buf_len       = 256;
  cfg.use_apll          = false;
  cfg.tx_desc_auto_clear = true;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num   = PIN_I2S_BCLK;
  pins.ws_io_num    = PIN_I2S_LRC;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) return false;

  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

// -----------------------------------------------------------------------------
// playTone — genera una senoide a freqHz durante durationMs y la envía por I2S.
// Aplica una envolvente attack/release corta para evitar clicks.
// -----------------------------------------------------------------------------
static void playTone(uint16_t freqHz, uint16_t durationMs) {
  if (freqHz == 0) {
    // silencio: enviar ceros
    const size_t totalSamples = (uint32_t)I2S_SAMPLE_RATE * durationMs / 1000;
    int16_t buf[128] = {0};
    size_t written;
    size_t remaining = totalSamples;
    while (remaining > 0 && !s_stopRequested) {
      size_t chunk = remaining > 128 ? 128 : remaining;
      i2s_write(I2S_PORT, buf, chunk * sizeof(int16_t), &written, portMAX_DELAY);
      remaining -= chunk;
    }
    return;
  }

  const float twoPiF = 2.0f * (float)M_PI * (float)freqHz / (float)I2S_SAMPLE_RATE;
  const uint32_t totalSamples = (uint32_t)I2S_SAMPLE_RATE * durationMs / 1000;
  const uint32_t attack = I2S_SAMPLE_RATE / 200;   // ~5 ms
  const uint32_t release = I2S_SAMPLE_RATE / 200;
  const float amp = 12000.0f;  // headroom para no saturar

  int16_t buf[128];
  uint32_t n = 0;
  while (n < totalSamples && !s_stopRequested) {
    uint32_t chunk = totalSamples - n;
    if (chunk > 128) chunk = 128;
    for (uint32_t i = 0; i < chunk; ++i) {
      float env = 1.0f;
      uint32_t pos = n + i;
      if (pos < attack)              env = (float)pos / attack;
      else if (pos > totalSamples - release)
                                     env = (float)(totalSamples - pos) / release;
      buf[i] = (int16_t)(amp * env * sinf(twoPiF * (float)pos));
    }
    size_t written;
    i2s_write(I2S_PORT, buf, chunk * sizeof(int16_t), &written, portMAX_DELAY);
    n += chunk;
  }
}

// -----------------------------------------------------------------------------
// playSong — reproduce un arreglo de Note (PROGMEM).
// -----------------------------------------------------------------------------
static void playSong(const Note* song, size_t len) {
  xEventGroupSetBits(evtSystem, EVT_AUDIO_PLAYING);
  for (size_t i = 0; i < len && !s_stopRequested; ++i) {
    Note n;
    memcpy_P(&n, &song[i], sizeof(Note));
    playTone(n.freqHz, n.ms);
  }
  i2s_zero_dma_buffer(I2S_PORT);
  xEventGroupClearBits(evtSystem, EVT_AUDIO_PLAYING);
}

// =============================================================================
// taskAudio — bloquea en qAudioCmd; reproduce melodías a demanda.
// =============================================================================
void taskAudio(void* arg) {
  AudioCmd cmd;
  for (;;) {
    if (xQueueReceive(qAudioCmd, &cmd, portMAX_DELAY) == pdTRUE) {
      s_stopRequested = false;
      switch (cmd) {
        case AUDIO_CMD_PLAY_WATER:
          logf("[audio] play water");
          playSong(SONG_WATER, SONG_WATER_LEN);
          break;
        case AUDIO_CMD_PLAY_ALERT:
          logf("[audio] play alert");
          playSong(SONG_ALERT, SONG_ALERT_LEN);
          break;
        case AUDIO_CMD_STOP:
          s_stopRequested = true;
          i2s_zero_dma_buffer(I2S_PORT);
          xEventGroupClearBits(evtSystem, EVT_AUDIO_PLAYING);
          break;
      }
    }
  }
}
