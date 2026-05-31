// =============================================================================
// leds.cpp — Patrones de los dos anillos NeoPixel.
//
// El patrón y el color se leen de settings en cada ciclo, así el dashboard
// (local o remoto por MQTT) puede cambiarlos en caliente. El brillo de cada
// anillo también se lee de settings y se aplica de forma independiente.
//
// Patrones soportados (deben coincidir con NEO_PATTERNS_CSV en config.h):
//   * rainbow  — arcoíris HSV rotando; el anillo 2 va desfasado y en sentido
//                opuesto (comportamiento histórico).
//   * solid    — color fijo elegido por el usuario en ambos anillos.
//   * off      — apagados.
//   * breathe  — color fijo con el brillo modulado suavemente (respiración).
//   * comet    — un punto del color recorre el anillo dejando una estela.
// =============================================================================
#include "leds.h"
#include "../core/config.h"
#include "../core/settings.h"
#include <Adafruit_NeoPixel.h>
#include <string.h>

static Adafruit_NeoPixel aroNeo1(NEOPIXEL_COUNT, PIN_NEOPIXEL_1,
                                 NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel aroNeo2(NEOPIXEL_COUNT, PIN_NEOPIXEL_2,
                                 NEO_GRB + NEO_KHZ800);

// =============================================================================
// leds_init — arranca ambos anillos con el brillo inicial.
// =============================================================================
bool leds_init() {
  aroNeo1.begin();
  aroNeo2.begin();
  aroNeo1.setBrightness(NEOPIXEL_BRIGHTNESS);
  aroNeo2.setBrightness(NEOPIXEL_BRIGHTNESS);
  aroNeo1.show();
  aroNeo2.show();
  return true;
}

// -----------------------------------------------------------------------------
// renderRainbow — arcoíris HSV rotando (los dos anillos desfasados/opuestos).
// -----------------------------------------------------------------------------
static void renderRainbow(uint16_t phase1, uint16_t phase2) {
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    aroNeo1.setPixelColor(i, aroNeo1.ColorHSV(phase1 + (i * 65536 / NEOPIXEL_COUNT)));
    aroNeo2.setPixelColor(i, aroNeo2.ColorHSV(phase2 + (i * 65536 / NEOPIXEL_COUNT)));
  }
}

// -----------------------------------------------------------------------------
// renderSolid — color fijo en ambos anillos.
// -----------------------------------------------------------------------------
static void renderSolid(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t c = Adafruit_NeoPixel::Color(r, g, b);
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    aroNeo1.setPixelColor(i, c);
    aroNeo2.setPixelColor(i, c);
  }
}

// -----------------------------------------------------------------------------
// renderComet — un punto recorre el anillo con estela atenuada.
// -----------------------------------------------------------------------------
static void renderComet(uint8_t r, uint8_t g, uint8_t b, uint16_t head) {
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    // Distancia (hacia atrás) del píxel i a la cabeza del cometa.
    int d = (head - i + NEOPIXEL_COUNT) % NEOPIXEL_COUNT;
    uint8_t f = (d == 0) ? 255 : (d == 1) ? 90 : (d == 2) ? 25 : 0;
    uint32_t c = Adafruit_NeoPixel::Color((r * f) / 255, (g * f) / 255, (b * f) / 255);
    aroNeo1.setPixelColor(i, c);
    aroNeo2.setPixelColor(i, c);
  }
}

// =============================================================================
// taskLED — Período 20 ms. Lee patrón/color/brillo de settings y renderiza.
// =============================================================================
void taskLED(void* arg) {
  uint16_t rainbowPhase1 = 0;
  uint16_t rainbowPhase2 = 32768;   // medio círculo de desfase
  uint16_t breathePhase  = 0;       // 0..65535 para la respiración
  uint16_t cometStep     = 0;       // contador para mover el cometa
  uint8_t  cometHead     = 0;

  uint8_t prevB1 = 0xFF, prevB2 = 0xFF;

  for (;;) {
    RuntimeSettings st;
    settings_get(st);

    // Para los patrones que NO modulan el brillo, aplicamos el brillo de
    // settings sólo cuando cambia (evita reescalar de más).
    bool breathing = (strcmp(st.neoPattern, "breathe") == 0);
    if (!breathing) {
      if (st.neoBrightness1 != prevB1) { aroNeo1.setBrightness(st.neoBrightness1); prevB1 = st.neoBrightness1; }
      if (st.neoBrightness2 != prevB2) { aroNeo2.setBrightness(st.neoBrightness2); prevB2 = st.neoBrightness2; }
    }

    if (strcmp(st.neoPattern, "off") == 0) {
      aroNeo1.clear();
      aroNeo2.clear();
    } else if (strcmp(st.neoPattern, "solid") == 0) {
      renderSolid(st.neoR, st.neoG, st.neoB);
    } else if (breathing) {
      // Color fijo con el brillo oscilando (triángulo) entre un piso y el
      // brillo configurado de cada anillo.
      uint16_t tri = breathePhase < 32768 ? breathePhase : (65535 - breathePhase);
      uint8_t floor1 = st.neoBrightness1 / 4;   // piso ~25%
      uint8_t floor2 = st.neoBrightness2 / 4;
      uint8_t lvl1 = floor1 + (uint32_t)(st.neoBrightness1 - floor1) * tri / 32768;
      uint8_t lvl2 = floor2 + (uint32_t)(st.neoBrightness2 - floor2) * tri / 32768;
      aroNeo1.setBrightness(lvl1); prevB1 = 0xFF;
      aroNeo2.setBrightness(lvl2); prevB2 = 0xFF;
      renderSolid(st.neoR, st.neoG, st.neoB);
      breathePhase += 700;
    } else if (strcmp(st.neoPattern, "comet") == 0) {
      renderComet(st.neoR, st.neoG, st.neoB, cometHead);
      if (++cometStep >= 5) { cometStep = 0; cometHead = (cometHead + 1) % NEOPIXEL_COUNT; }
    } else {
      // "rainbow" (por defecto).
      renderRainbow(rainbowPhase1, rainbowPhase2);
      rainbowPhase1 += 256;
      rainbowPhase2 -= 256;
    }

    aroNeo1.show();
    aroNeo2.show();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
