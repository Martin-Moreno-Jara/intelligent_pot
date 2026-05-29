// =============================================================================
// leds.cpp — Animación decorativa de los dos anillos NeoPixel.
//
// Arcoíris HSV que rota: el anillo 1 avanza y el anillo 2 retrocede (desfasados
// medio círculo), tal como en el código de prueba. El brillo de cada anillo se
// lee de settings en cada ciclo, así el dashboard puede cambiarlo en caliente
// e independientemente.
// =============================================================================
#include "leds.h"
#include "../core/config.h"
#include "../core/settings.h"
#include <Adafruit_NeoPixel.h>

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

// =============================================================================
// taskLED — Período 20 ms. Renderiza el arcoíris y aplica el brillo de settings.
// =============================================================================
void taskLED(void* arg) {
  uint16_t animDesfase1 = 0;
  uint16_t animDesfase2 = 32768;   // medio círculo de desfase

  uint8_t prevB1 = 0xFF, prevB2 = 0xFF;

  for (;;) {
    // Brillo independiente por anillo (ajustable desde el dashboard).
    RuntimeSettings st;
    settings_get(st);
    if (st.neoBrightness1 != prevB1) {
      aroNeo1.setBrightness(st.neoBrightness1);
      prevB1 = st.neoBrightness1;
    }
    if (st.neoBrightness2 != prevB2) {
      aroNeo2.setBrightness(st.neoBrightness2);
      prevB2 = st.neoBrightness2;
    }

    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      aroNeo1.setPixelColor(i, aroNeo1.ColorHSV(animDesfase1 + (i * 65536 / NEOPIXEL_COUNT)));
      aroNeo2.setPixelColor(i, aroNeo2.ColorHSV(animDesfase2 + (i * 65536 / NEOPIXEL_COUNT)));
    }
    aroNeo1.show();
    aroNeo2.show();

    animDesfase1 += 256;
    animDesfase2 -= 256;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
