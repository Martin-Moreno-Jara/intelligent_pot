#pragma once
// =============================================================================
// leds.h — Dos anillos NeoPixel DECORATIVOS.
//
// Ya NO son indicadores de humedad/ambiente: renderizan el patrón elegido por
// el usuario (rainbow/solid/off/breathe/comet, ver NEO_PATTERNS_CSV). El patrón,
// el color y el brillo de cada anillo se leen de settings.h en caliente, así que
// pueden cambiarse desde el dashboard local o de forma remota por MQTT.
// =============================================================================

#include "../core/tasks.h"

bool leds_init();
void taskLED(void* arg);
