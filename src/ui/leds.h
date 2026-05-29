#pragma once
// =============================================================================
// leds.h — Dos anillos NeoPixel DECORATIVOS.
//
// Ya NO son indicadores de humedad/ambiente: muestran una animación de color
// (arcoíris HSV rotando en sentidos opuestos, igual que el código de prueba).
// El brillo de cada anillo se ajusta de forma independiente desde el dashboard
// web (settings.h: neoBrightness1 / neoBrightness2).
// =============================================================================

#include "../core/tasks.h"

bool leds_init();
void taskLED(void* arg);
