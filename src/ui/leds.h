#pragma once
// =============================================================================
// leds.h — Dos anillos NeoPixel:
//   * Anillo 1: nivel de humedad de suelo (gradiente rojo->verde->azul).
//   * Anillo 2: estado ambiental (T, AQI, lux) combinado.
// =============================================================================

#include "../core/tasks.h"

bool leds_init();
void taskLED(void* arg);
