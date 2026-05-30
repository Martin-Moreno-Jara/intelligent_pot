// =============================================================================
// settings.cpp — Storage + acceso protegido por mutex + persistencia NVS.
//
// Al arrancar, settings_init() carga los valores desde NVS (Preferences).
// Si una clave no existe todavía (primer boot) usa el #define de config.h.
// Cada setter persiste automáticamente en NVS vía withLock → nvsSave().
// Además marca EVT_STATE_DIRTY para que taskMQTT republique MQTT_TOPIC_STATE.
// =============================================================================
#include "settings.h"
#include "config.h"
#include "tasks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <Preferences.h>
#include <string.h>

// Namespace NVS exclusivo para los ajustes del dashboard (≤15 chars).
#define CFG_NS "matera_cfg"

static RuntimeSettings   s_cfg;
static SemaphoreHandle_t s_mtx = nullptr;

// -----------------------------------------------------------------------------
// NVS helpers — llamar solo con s_mtx tomado (o antes de lanzar tareas).
// -----------------------------------------------------------------------------
static void nvsSave() {
  Preferences p;
  p.begin(CFG_NS, /*readOnly=*/false);
  p.putFloat ("soil_thr",  s_cfg.soilThresholdLowPct);
  p.putFloat ("irr_hrs",   s_cfg.irrigationIntervalHrs);
  p.putUChar ("neo_br1",   s_cfg.neoBrightness1);
  p.putUChar ("neo_br2",   s_cfg.neoBrightness2);
  p.putInt   ("audio_vol", s_cfg.audioVolume);
  p.putString("neo_pat",   s_cfg.neoPattern);
  p.putUChar ("neo_r",     s_cfg.neoR);
  p.putUChar ("neo_g",     s_cfg.neoG);
  p.putUChar ("neo_b",     s_cfg.neoB);
  p.end();
}

static void nvsLoad() {
  Preferences p;
  p.begin(CFG_NS, /*readOnly=*/true);
  s_cfg.soilThresholdLowPct   = p.getFloat ("soil_thr",  SOIL_MOISTURE_THRESHOLD_LOW_PCT);
  s_cfg.irrigationIntervalHrs = p.getFloat ("irr_hrs",   IRRIGATION_INTERVAL_HRS_DEFAULT);
  s_cfg.neoBrightness1        = p.getUChar ("neo_br1",   NEOPIXEL_BRIGHTNESS);
  s_cfg.neoBrightness2        = p.getUChar ("neo_br2",   NEOPIXEL_BRIGHTNESS);
  s_cfg.audioVolume           = p.getInt   ("audio_vol", AUDIO_VOLUME_DEFAULT);
  String pat = p.getString("neo_pat", NEO_PATTERN_DEFAULT);
  strncpy(s_cfg.neoPattern, pat.c_str(), sizeof(s_cfg.neoPattern) - 1);
  s_cfg.neoPattern[sizeof(s_cfg.neoPattern) - 1] = '\0';
  s_cfg.neoR = p.getUChar("neo_r", NEO_COLOR_R_DEFAULT);
  s_cfg.neoG = p.getUChar("neo_g", NEO_COLOR_G_DEFAULT);
  s_cfg.neoB = p.getUChar("neo_b", NEO_COLOR_B_DEFAULT);
  p.end();
}

// -----------------------------------------------------------------------------
// API pública
// -----------------------------------------------------------------------------
void settings_init() {
  s_mtx = xSemaphoreCreateMutex();
  // nvsLoad usa los #define como fallback si la clave no existe (primer boot).
  nvsLoad();
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

// Aplica cambio bajo mutex, persiste en NVS y marca estado como sucio.
template <typename Fn>
static void withLock(Fn fn) {
  if (s_mtx && xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
    fn();
    nvsSave();
    xSemaphoreGive(s_mtx);
  } else {
    fn();
    nvsSave();
  }
  markDirty();
}

void settings_setThresholdLow(float pct) {
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  withLock([&]() { s_cfg.soilThresholdLowPct = pct; });
}

void settings_setIrrigationInterval(float hrs) {
  if (hrs < 0.1f) hrs = 0.1f;
  if (hrs > 720)  hrs = 720;
  withLock([&]() { s_cfg.irrigationIntervalHrs = hrs; });
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

void settings_setVolume(int level) {
  if (level < 0)                level = 0;
  if (level > AUDIO_VOLUME_MAX) level = AUDIO_VOLUME_MAX;
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
