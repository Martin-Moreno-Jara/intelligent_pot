// =============================================================================
// sensors.cpp — Implementación de la tarea de adquisición de sensores.
// Bloquea mtxI2C durante las lecturas I2C; el ADC y el DHT11 no necesitan
// mutex porque sólo los usa esta tarea.
// =============================================================================
#include "sensors.h"
#include "../core/config.h"
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>

static DHT     dht(PIN_DHT, DHT11);
static BH1750  bh1750;

static bool s_dhtReady    = false;
static bool s_bh1750Ready = false;

// -----------------------------------------------------------------------------
// readSoilMoisturePct — convierte el ADC del SEN0193 a porcentaje.
// Mapeo lineal con clamp 0..100. La constante DRY/WET vive en config.h.
// -----------------------------------------------------------------------------
static float readSoilMoisturePct(uint16_t& rawOut) {
  uint32_t acc = 0;
  const int N = 8;
  for (int i = 0; i < N; ++i) acc += analogRead(PIN_SOIL_SENSOR);
  uint16_t raw = acc / N;
  rawOut = raw;
  float pct = 100.0f * (float)(SOIL_RAW_DRY - raw) /
                       (float)(SOIL_RAW_DRY - SOIL_RAW_WET);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// -----------------------------------------------------------------------------
// readMQ135Ppm — Estimación muy básica de CO2-eq.
// El MQ-135 necesita calibración seria para valores absolutos; aquí se hace
// una conversión lineal indicativa para tener un valor publicable.
// -----------------------------------------------------------------------------
static float readMQ135Ppm() {
  uint32_t acc = 0;
  const int N = 8;
  for (int i = 0; i < N; ++i) acc += analogRead(PIN_MQ135);
  float raw = (float)(acc / N);
  // 400 ppm aire limpio ~ raw 800; 2000 ppm ~ raw 2500 (aproximación).
  float ppm = 400.0f + (raw - 800.0f) * (1600.0f / 1700.0f);
  if (ppm < 350.0f) ppm = 350.0f;
  return ppm;
}

// =============================================================================
// sensors_init — Inicializa I2C compartido, DHT11 y cada sensor I2C.
// =============================================================================
bool sensors_init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

  analogReadResolution(ADC_RESOLUTION_BITS);
  analogSetPinAttenuation(PIN_SOIL_SENSOR, ADC_11db);
  analogSetPinAttenuation(PIN_MQ135,       ADC_11db);

  dht.begin();
  s_dhtReady = true;

  s_bh1750Ready = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  logf("[sensors] DHT11:%d  BH1750:%d", s_dhtReady, s_bh1750Ready);
  return s_dhtReady || s_bh1750Ready;
}

// =============================================================================
// taskSensors — Período 2 s. Lee todos los sensores, publica snapshot.
// =============================================================================
void taskSensors(void* arg) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(2000);

  SensorData data{};
  for (;;) {
    // ---- lecturas analógicas (sin I2C, no requiere mutex) ----
    uint16_t raw = 0;
    data.soilMoisturePct = readSoilMoisturePct(raw);
    data.soilRaw         = raw;
    data.airQualityPpm   = readMQ135Ppm();

    // ---- lectura DHT11 (1-Wire, sin mutex) ----
    if (s_dhtReady) {
      float h = dht.readHumidity();
      float t = dht.readTemperature();
      if (!isnan(h) && !isnan(t)) {
        data.tempC  = t;
        data.humPct = h;
        data.dhtOk  = true;
      } else {
        data.dhtOk = false;
      }
    }

    // ---- lecturas I2C (protegidas por mtxI2C) ----
    if (xSemaphoreTake(mtxI2C, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (s_bh1750Ready) {
        float lx = bh1750.readLightLevel();
        if (lx >= 0) {
          data.lux      = lx;
          data.bh1750Ok = true;
        }
      }
      xSemaphoreGive(mtxI2C);
    }

    data.timestampMs = millis();

    // Publicar snapshot (sobrescribe el anterior; consumidores hacen peek).
    xQueueOverwrite(qSensorData, &data);

    logf("[sensors] soil=%.1f%% T=%.1fC H=%.1f%% lux=%.0f ppm=%.0f",
         data.soilMoisturePct, data.tempC, data.humPct,
         data.lux, data.airQualityPpm);

    vTaskDelayUntil(&lastWake, period);
  }
}
