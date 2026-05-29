#pragma once
// =============================================================================
// irrigation.h — Control de la bomba con riego POR PULSOS.
//
// En vez de mantener la bomba encendida hasta alcanzar un objetivo (riesgo de
// sobre-riego porque el sensor tarda en reflejar el cambio), el ciclo es:
//   1. humedad < umbral  -> pulso de bomba (IRRIGATION_PULSE_MS, ~3-4 s)
//   2. apagar y esperar  (IRRIGATION_SETTLE_MS, ~10 s) a que el agua se absorba
//   3. re-medir: si sigue seco, otro pulso; si ya superó el umbral, terminar.
//
// El umbral es ajustable en caliente desde el dashboard (settings.h). También
// acepta comandos manuales por qIrrigationCmd (botón "regar", MQTT). Al iniciar
// un ciclo dispara la canción de riego configurada. El servo NO se controla
// aquí: gira de forma continua e independiente (servo360.h).
// =============================================================================

#include "../core/tasks.h"

bool irrigation_init();
void taskIrrigation(void* arg);
