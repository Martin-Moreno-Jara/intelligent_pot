#pragma once
// =============================================================================
// irrigation.h — Riego por intervalo + umbral con driver DRV8833 (puente H).
//
// Auto:   cada irrigationIntervalHrs, si soil < soilThresholdLowPct → bomba 5 s.
// Manual: comando MQTT "1" → bomba 5 s inmediatos (no afecta el temporizador).
// =============================================================================

#include "../core/tasks.h"

bool irrigation_init();
void taskIrrigation(void* arg);
