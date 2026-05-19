#pragma once
// =============================================================================
// sensors.h — Lectura de AHT20, BMP280, BH1750, MQ-135 y SEN0193.
// Sólo expone init y la función de la tarea; la tarea publica SensorData
// en qSensorData por overwrite.
// =============================================================================

#include "../core/tasks.h"

bool sensors_init();         // inicializa I2C, AHT20, BMP280, BH1750, ADC
void taskSensors(void* arg); // tarea FreeRTOS (período 2 s)
