#pragma once
// =============================================================================
// settings.h — Configuración ajustable en caliente desde el dashboard web.
//
// Estos parámetros se modifican desde el navegador o por MQTT (no son #define
// fijos) y los consumen las tareas de riego, LEDs y audio:
//   * soilThresholdPct  -> umbral de humedad de tierra para iniciar el riego.
//   * neoBrightness1/2   -> brillo independiente de cada anillo NeoPixel (0..255).
//   * neoPattern         -> nombre del patrón NeoPixel (ver NEO_PATTERNS_CSV).
//   * neoR/G/B           -> color para los patrones de color sólido.
//   * audioVolume        -> nivel de volumen actual (0..AUDIO_VOLUME_MAX).
//   * wateringSongIndex  -> índice del MP3 (en la SD) que suena al regar; -1=ninguno.
//
// El acceso está protegido por un mutex interno; usar siempre los getters/setters.
// Cada setter marca EVT_STATE_DIRTY para que la tarea MQTT republique el estado.
// =============================================================================

#include <Arduino.h>

struct RuntimeSettings {
  float    soilThresholdPct;   // % por debajo del cual se riega
  uint8_t  neoBrightness1;     // 0..255 anillo 1
  uint8_t  neoBrightness2;     // 0..255 anillo 2
  int      wateringSongIndex;  // índice de canción de riego; -1 = ninguna
  int      audioVolume;        // 0..AUDIO_VOLUME_MAX (espejo del nivel de audio)
  char     neoPattern[16];     // "rainbow","solid","off","breathe","comet"
  uint8_t  neoR, neoG, neoB;   // color para los patrones de color sólido
};

// Crea el mutex y carga los valores por defecto desde config.h. Llamar una vez
// antes de lanzar tareas.
void settings_init();

// Copia atómica del estado completo (para lectores).
void settings_get(RuntimeSettings& out);

// Setters individuales (cada uno valida/satura su rango).
void settings_setThreshold(float pct);        // saturado a [0, 100]
void settings_setBrightness1(uint8_t b);
void settings_setBrightness2(uint8_t b);
void settings_setBrightnessBoth(uint8_t b);    // ambos anillos a la vez
void settings_setWateringSong(int idx);        // -1 o índice válido
void settings_setVolume(int level);            // saturado a [0, AUDIO_VOLUME_MAX]
void settings_setNeoPattern(const char* name); // copia segura (truncada a 15)
void settings_setNeoColor(uint8_t r, uint8_t g, uint8_t b);
