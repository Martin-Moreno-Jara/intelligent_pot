// =============================================================================
// sensors.cpp — Implementación de la tarea de adquisición de sensores.
// Bloquea mtxI2C durante las lecturas I2C; el ADC no necesita mutex porque
// sólo lo usa esta tarea.
// =============================================================================
#include "sensors.h"
#include "../core/config.h"
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>

static Adafruit_AHTX0   aht;
static Adafruit_BMP280  bmp;
static BH1750           bh1750;

static bool s_ahtReady    = false;
static bool s_bmpReady    = false;
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
// sensors_init — Inicializa I2C compartido y cada sensor I2C.
// =============================================================================
bool sensors_init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

  analogReadResolution(ADC_RESOLUTION_BITS);
  analogSetPinAttenuation(PIN_SOIL_SENSOR, ADC_11db);
  analogSetPinAttenuation(PIN_MQ135,       ADC_11db);

  s_ahtReady = aht.begin();
  // BMP280 puede estar en 0x76 o 0x77; intentar ambas.
  s_bmpReady = bmp.begin(0x76) || bmp.begin(0x77);
  if (s_bmpReady) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
  }
  s_bh1750Ready = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  logf("[sensors] AHT20:%d  BMP280:%d  BH1750:%d",
       s_ahtReady, s_bmpReady, s_bh1750Ready);
  return s_ahtReady || s_bmpReady || s_bh1750Ready;
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

    // ---- lecturas I2C (protegidas por mtxI2C) ----
    if (xSemaphoreTake(mtxI2C, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (s_ahtReady) {
        sensors_event_t hum, tmp;
        if (aht.getEvent(&hum, &tmp)) {
          data.tempC  = tmp.temperature;
          data.humPct = hum.relative_humidity;
          data.ahtOk  = true;
        } else {
          data.ahtOk = false;
        }
      }
      if (s_bmpReady) {
        data.pressureHPa = bmp.readPressure() / 100.0f;
        data.bmpOk       = true;
      }
      if (s_bh1750Ready) {
        float lx = bh1750.readLightLevel();
        if (lx >= 0) {
          data.lux       = lx;
          data.bh1750Ok  = true;
        }
      }
      xSemaphoreGive(mtxI2C);
    }

    data.timestampMs = millis();

    // Publicar snapshot (sobrescribe el anterior; consumidores hacen peek).
    xQueueOverwrite(qSensorData, &data);

    logf("[sensors] soil=%.1f%% T=%.1fC H=%.1f%% P=%.1fhPa lux=%.0f ppm=%.0f",
         data.soilMoisturePct, data.tempC, data.humPct,
         data.pressureHPa, data.lux, data.airQualityPpm);

    vTaskDelayUntil(&lastWake, period);
  }
}
