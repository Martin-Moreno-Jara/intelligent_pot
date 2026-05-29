// =============================================================================
// settings.cpp — Storage + acceso protegido por mutex de la config en caliente.
// =============================================================================
#include "settings.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static RuntimeSettings    s_cfg;
static SemaphoreHandle_t  s_mtx = nullptr;

void settings_init() {
  s_mtx = xSemaphoreCreateMutex();
  s_cfg.soilThresholdPct  = SOIL_MOISTURE_TRIGGER_PCT;
  s_cfg.neoBrightness1    = NEOPIXEL_BRIGHTNESS;
  s_cfg.neoBrightness2    = NEOPIXEL_BRIGHTNESS;
  s_cfg.wateringSongIndex = -1;   // sin canción hasta que el usuario elija una
}

void settings_get(RuntimeSettings& out) {
  if (s_mtx && xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
    out = s_cfg;
    xSemaphoreGive(s_mtx);
  } else {
    out = s_cfg;
  }
}

// Helper: aplica un cambio bajo el mutex.
template <typename Fn>
static void withLock(Fn fn) {
  if (s_mtx && xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
    fn();
    xSemaphoreGive(s_mtx);
  } else {
    fn();
  }
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

void settings_setWateringSong(int idx) {
  withLock([&]() { s_cfg.wateringSongIndex = idx; });
}
