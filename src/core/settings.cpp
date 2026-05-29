// =============================================================================
// settings.cpp — Storage + acceso protegido por mutex de la config en caliente.
//
// Cada setter marca EVT_STATE_DIRTY en evtSystem para que taskMQTT republique
// MQTT_TOPIC_STATE (retained) y la Raspberry Pi vea el cambio al instante.
// =============================================================================
#include "settings.h"
#include "config.h"
#include "tasks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static RuntimeSettings    s_cfg;
static SemaphoreHandle_t  s_mtx = nullptr;

void settings_init() {
  s_mtx = xSemaphoreCreateMutex();
  s_cfg.soilThresholdPct  = SOIL_MOISTURE_TRIGGER_PCT;
  s_cfg.neoBrightness1    = NEOPIXEL_BRIGHTNESS;
  s_cfg.neoBrightness2    = NEOPIXEL_BRIGHTNESS;
  s_cfg.wateringSongIndex = -1;   // sin canción hasta que el usuario elija una
  s_cfg.audioVolume       = AUDIO_VOLUME_DEFAULT;
  strncpy(s_cfg.neoPattern, NEO_PATTERN_DEFAULT, sizeof(s_cfg.neoPattern) - 1);
  s_cfg.neoPattern[sizeof(s_cfg.neoPattern) - 1] = '\0';
  s_cfg.neoR = NEO_COLOR_R_DEFAULT;
  s_cfg.neoG = NEO_COLOR_G_DEFAULT;
  s_cfg.neoB = NEO_COLOR_B_DEFAULT;
}

void settings_get(RuntimeSettings& out) {
  if (s_mtx && xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
    out = s_cfg;
    xSemaphoreGive(s_mtx);
  } else {
    out = s_cfg;
  }
}

// Avisa a taskMQTT que debe republicar el estado retenido.
static inline void markDirty() {
  if (evtSystem) xEventGroupSetBits(evtSystem, EVT_STATE_DIRTY);
}

// Helper: aplica un cambio bajo el mutex y marca el estado como sucio.
template <typename Fn>
static void withLock(Fn fn) {
  if (s_mtx && xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
    fn();
    xSemaphoreGive(s_mtx);
  } else {
    fn();
  }
  markDirty();
}

void settings_setThreshold(float pct) {
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  withLock([&]() { s_cfg.soilThresholdPct = pct; });
}

void settings_setBrightness1(uint8_t b) {
  withLock([&]() { s_cfg.neoBrightness1 = b; });
}

void settings_setBrightness2(uint8_t b) {
  withLock([&]() { s_cfg.neoBrightness2 = b; });
}

void settings_setBrightnessBoth(uint8_t b) {
  withLock([&]() { s_cfg.neoBrightness1 = b; s_cfg.neoBrightness2 = b; });
}

void settings_setWateringSong(int idx) {
  withLock([&]() { s_cfg.wateringSongIndex = idx; });
}

void settings_setVolume(int level) {
  if (level < 0)                 level = 0;
  if (level > AUDIO_VOLUME_MAX)  level = AUDIO_VOLUME_MAX;
  withLock([&]() { s_cfg.audioVolume = level; });
}

void settings_setNeoPattern(const char* name) {
  if (!name) return;
  withLock([&]() {
    strncpy(s_cfg.neoPattern, name, sizeof(s_cfg.neoPattern) - 1);
    s_cfg.neoPattern[sizeof(s_cfg.neoPattern) - 1] = '\0';
  });
}

void settings_setNeoColor(uint8_t r, uint8_t g, uint8_t b) {
  withLock([&]() { s_cfg.neoR = r; s_cfg.neoG = g; s_cfg.neoB = b; });
}
