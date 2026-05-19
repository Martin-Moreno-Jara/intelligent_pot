#pragma once
// =============================================================================
// tasks.h — Declaración de las primitivas FreeRTOS compartidas:
//   * Estructuras de mensaje
//   * Colas (QueueHandle_t)
//   * Mutex (SemaphoreHandle_t)
//   * Event group (banderas de estado del sistema)
//   * Task handles
// Las definiciones (storage) viven en tasks.cpp.
// =============================================================================

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

// ----- Estructuras de mensaje ------------------------------------------------

// Snapshot de todos los sensores. Producido por TaskSensors, consumido por
// TaskIrrigation, TaskLED, TaskDisplay, TaskMQTT, TaskThingSpeak.
struct SensorData {
  float    soilMoisturePct;   // 0..100
  uint16_t soilRaw;           // ADC bruto (para debug)
  float    tempC;
  float    humPct;
  float    pressureHPa;
  float    lux;
  float    airQualityPpm;     // CO2-eq estimado por MQ-135
  uint32_t timestampMs;
  bool     ahtOk;
  bool     bmpOk;
  bool     bh1750Ok;
};

// Comando de riego enviado a TaskIrrigation
enum IrrigationCmd : uint8_t {
  IRR_CMD_MANUAL_START = 1,
  IRR_CMD_STOP         = 2
};

// Comando de audio enviado a TaskAudio
enum AudioCmd : uint8_t {
  AUDIO_CMD_PLAY_WATER = 1,   // melodía cuando inicia el riego
  AUDIO_CMD_PLAY_ALERT = 2,   // melodía por petición MQTT u alerta
  AUDIO_CMD_STOP       = 3
};

// ----- Colas (definidas en tasks.cpp) ----------------------------------------
// qSensorData: longitud 1, usada con xQueueOverwrite/xQueuePeek para
// distribuir el snapshot más reciente a todos los consumidores sin bloquear.
extern QueueHandle_t qSensorData;
extern QueueHandle_t qIrrigationCmd;   // comandos -> TaskIrrigation
extern QueueHandle_t qAudioCmd;        // comandos -> TaskAudio

// ----- Mutex -----------------------------------------------------------------
extern SemaphoreHandle_t mtxI2C;       // protege el bus I2C (sensores + OLED)
extern SemaphoreHandle_t mtxSerial;    // protege Serial para logs limpios

// ----- Event group de estado -------------------------------------------------
extern EventGroupHandle_t evtSystem;
#define EVT_WIFI_CONNECTED    (1 << 0)
#define EVT_MQTT_CONNECTED    (1 << 1)
#define EVT_IRRIGATING        (1 << 2)
#define EVT_AUDIO_PLAYING     (1 << 3)

// ----- Task handles ----------------------------------------------------------
extern TaskHandle_t hTaskSensors;
extern TaskHandle_t hTaskIrrigation;
extern TaskHandle_t hTaskAudio;
extern TaskHandle_t hTaskLED;
extern TaskHandle_t hTaskDisplay;
extern TaskHandle_t hTaskMQTT;
extern TaskHandle_t hTaskThingSpeak;

// ----- API: crear primitivas y lanzar tareas ---------------------------------
void tasks_initPrimitives();   // crea colas, mutex, event group
void tasks_startAll();         // lanza todas las tareas FreeRTOS

// ----- Helper: log thread-safe ----------------------------------------------
void logf(const char* fmt, ...);
