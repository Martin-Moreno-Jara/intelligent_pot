// =============================================================================
// leds.cpp — Animación de los dos anillos NeoPixel.
// La tarea se ejecuta cada 100 ms; lee el snapshot de sensores con peek y
// también consulta evtSystem para reaccionar a EVT_IRRIGATING.
// =============================================================================
#include "leds.h"
#include "../core/config.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel ringSoil(NEOPIXEL_COUNT, PIN_NEOPIXEL_SOIL,
                                  NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel ringEnv (NEOPIXEL_COUNT, PIN_NEOPIXEL_ENV,
                                  NEO_GRB + NEO_KHZ800);

// -----------------------------------------------------------------------------
// soilColor — color del nivel de humedad de suelo (0..100%).
// 0%   = rojo  | 50%  = verde | 100% = azul
// -----------------------------------------------------------------------------
static uint32_t soilColor(float pct) {
  if (pct < 50.0f) {
    float t = pct / 50.0f;            // 0..1
    uint8_t r = (uint8_t)((1.0f - t) * 255.0f);
    uint8_t g = (uint8_t)(t * 255.0f);
    return Adafruit_NeoPixel::Color(r, g, 0);
  } else {
    float t = (pct - 50.0f) / 50.0f;  // 0..1
    uint8_t g = (uint8_t)((1.0f - t) * 255.0f);
    uint8_t b = (uint8_t)(t * 255.0f);
    return Adafruit_NeoPixel::Color(0, g, b);
  }
}

// -----------------------------------------------------------------------------
// envColor — color del estado ambiental.
// Mezcla calidad del aire (rojo si alto), luz (amarillo) y temperatura.
// -----------------------------------------------------------------------------
static uint32_t envColor(const SensorData& s) {
  // AQI: 400 ppm = bueno; 1500+ = malo.
  float aqiBad = (s.airQualityPpm - 400.0f) / 1100.0f;
  if (aqiBad < 0) aqiBad = 0;
  if (aqiBad > 1) aqiBad = 1;
  // luz: 0 lx = 0, 1000+ lx = 1
  float lumNorm = s.lux / 1000.0f;
  if (lumNorm > 1) lumNorm = 1;
  // temperatura: 18..32 °C mapeado 0..1
  float tNorm = (s.tempC - 18.0f) / 14.0f;
  if (tNorm < 0) tNorm = 0;
  if (tNorm > 1) tNorm = 1;

  uint8_t r = (uint8_t)(255 * (0.5f * aqiBad + 0.5f * tNorm));
  uint8_t g = (uint8_t)(255 * (1.0f - aqiBad) * 0.7f);
  uint8_t b = (uint8_t)(255 * lumNorm * 0.4f);
  return Adafruit_NeoPixel::Color(r, g, b);
}

// =============================================================================
// leds_init — inicializa ambos anillos.
// =============================================================================
bool leds_init() {
  ringSoil.begin();
  ringEnv.begin();
  ringSoil.setBrightness(NEOPIXEL_BRIGHTNESS);
  ringEnv.setBrightness(NEOPIXEL_BRIGHTNESS);
  ringSoil.clear(); ringSoil.show();
  ringEnv.clear();  ringEnv.show();
  return true;
}

// =============================================================================
// taskLED — Período 100 ms. Renderiza ambos anillos.
// =============================================================================
void taskLED(void* arg) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(100);
  SensorData snap{};
  uint8_t pulse = 0;
  bool pulseDir = true;

  for (;;) {
    bool haveSnap = (xQueuePeek(qSensorData, &snap, 0) == pdTRUE);
    bool irrigating =
        (xEventGroupGetBits(evtSystem) & EVT_IRRIGATING) != 0;

    // ---- Anillo de humedad: gradiente, número de LEDs según % ----
    ringSoil.clear();
    if (haveSnap) {
      int n = (int)((snap.soilMoisturePct / 100.0f) * NEOPIXEL_COUNT + 0.5f);
      if (n > NEOPIXEL_COUNT) n = NEOPIXEL_COUNT;
      uint32_t c = soilColor(snap.soilMoisturePct);
      for (int i = 0; i < n; ++i) ringSoil.setPixelColor(i, c);

      // Si está regando: pulsar el anillo (efecto de "actividad").
      if (irrigating) {
        if (pulseDir) { if (++pulse > 200) pulseDir = false; }
        else          { if (--pulse < 30)  pulseDir = true;  }
        ringSoil.setBrightness(pulse);
      } else {
        ringSoil.setBrightness(NEOPIXEL_BRIGHTNESS);
      }
    }
    ringSoil.show();

    // ---- Anillo ambiental: color sólido mezclado ----
    ringEnv.clear();
    if (haveSnap) {
      uint32_t c = envColor(snap);
      for (int i = 0; i < NEOPIXEL_COUNT; ++i) ringEnv.setPixelColor(i, c);
    }
    ringEnv.show();

    vTaskDelayUntil(&lastWake, period);
  }
}
