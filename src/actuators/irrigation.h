#pragma once
// =============================================================================
// irrigation.h — Control de bomba (MOSFET) + servo de ducha.
// Lógica:
//   * lee snapshot de sensores (peek en qSensorData)
//   * si humedad < umbral y cooldown vencido -> inicia riego
//   * notifica a audio (canción) y enciende EVT_IRRIGATING
// También acepta comandos manuales via qIrrigationCmd (MQTT, etc.)
// =============================================================================

#include "../core/tasks.h"

bool irrigation_init();
void taskIrrigation(void* arg);
