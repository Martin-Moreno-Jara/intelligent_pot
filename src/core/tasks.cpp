// =============================================================================
// tasks.cpp — Storage de las primitivas FreeRTOS + arranque centralizado de
// las tareas del sistema.
// =============================================================================
#include "tasks.h"
#include "config.h"
#include "../sensors/sensors.h"
#include "../actuators/irrigation.h"
#include "../actuators/audio.h"
#include "../actuators/servo360.h"
#include "../ui/leds.h"
#include "../ui/display.h"
#include "../network/network.h"
#include "../network/web_dashboard.h"
#include <stdarg.h>

// ----- Colas -----------------------------------------------------------------
QueueHandle_t qSensorData    = nullptr;
QueueHandle_t qIrrigationCmd = nullptr;
QueueHandle_t qAudioCmd      = nullptr;

// ----- Mutex -----------------------------------------------------------------
SemaphoreHandle_t mtxI2C    = nullptr;
SemaphoreHandle_t mtxSerial = nullptr;

// ----- Event group -----------------------------------------------------------
EventGroupHandle_t evtSystem = nullptr;

// ----- Handles ---------------------------------------------------------------
TaskHandle_t hTaskSensors    = nullptr;
TaskHandle_t hTaskIrrigation = nullptr;
TaskHandle_t hTaskAudio      = nullptr;
TaskHandle_t hTaskServo      = nullptr;
TaskHandle_t hTaskLED        = nullptr;
TaskHandle_t hTaskDisplay    = nullptr;
TaskHandle_t hTaskMQTT       = nullptr;
TaskHandle_t hTaskDashboard  = nullptr;

// =============================================================================
// tasks_initPrimitives
// Crea TODAS las primitivas de sincronización antes de lanzar tareas.
// =============================================================================
void tasks_initPrimitives() {
  // Cola de sensores: longitud 1, escritura con xQueueOverwrite, lectura con
  // xQueuePeek (no destructiva) para multi-consumidor.
  qSensorData    = xQueueCreate(1, sizeof(SensorData));
  qIrrigationCmd = xQueueCreate(4, sizeof(IrrigationCmd));
  qAudioCmd      = xQueueCreate(8, sizeof(int));

  mtxI2C    = xSemaphoreCreateMutex();
  mtxSerial = xSemaphoreCreateMutex();

  evtSystem = xEventGroupCreate();

  configASSERT(qSensorData && qIrrigationCmd && qAudioCmd);
  configASSERT(mtxI2C && mtxSerial && evtSystem);
}

// =============================================================================
// tasks_startAll
// Lanza cada tarea con su stack y prioridad. Las tareas de red se anclan al
// core 0 (donde corren WiFi/BT por defecto) y el resto al core 1.
// =============================================================================
void tasks_startAll() {
  xTaskCreatePinnedToCore(taskSensors,    "Sensors",    TASK_STACK_SENSORS,
                          nullptr, PRIO_MEDIUM, &hTaskSensors,    1);
  xTaskCreatePinnedToCore(taskIrrigation, "Irrigation", TASK_STACK_IRRIGATION,
                          nullptr, PRIO_HIGH,   &hTaskIrrigation, 1);
  // Audio: prioridad más alta para que el decodificador MP3 no se corte.
  // Anclado al core 1 (app core) para aislarlo del stack WiFi/MQTT del core 0;
  // lee la SD por su bus HSPI propio.
  xTaskCreatePinnedToCore(taskAudio,      "Audio",      TASK_STACK_AUDIO,
                          nullptr, PRIO_HIGHER, &hTaskAudio,      1);
  // Servo de rotación continua: tarea liviana, core 1.
  xTaskCreatePinnedToCore(taskServo,      "Servo",      TASK_STACK_SERVO,
                          nullptr, PRIO_LOW,    &hTaskServo,      1);
  xTaskCreatePinnedToCore(taskLED,        "LED",        TASK_STACK_LED,
                          nullptr, PRIO_LOW,    &hTaskLED,        1);
  xTaskCreatePinnedToCore(taskDisplay,    "Display",    TASK_STACK_DISPLAY,
                          nullptr, PRIO_LOW,    &hTaskDisplay,    1);
  // Una sola tarea de red: ThingSpeak es ahora el único broker MQTT, así que
  // tanto la publicación de sensores como la recepción de comandos pasan por
  // taskMQTT (anclada al core 0, junto al stack WiFi).
  xTaskCreatePinnedToCore(taskMQTT,       "MQTT",       TASK_STACK_MQTT,
                          nullptr, PRIO_MEDIUM, &hTaskMQTT,       0);
  // Dashboard local: WebServer en la IP de la STA. Mismo core que el stack
  // WiFi para evitar trasiego entre cores en cada request.
  xTaskCreatePinnedToCore(taskDashboard,  "Dashboard",  TASK_STACK_DASHBOARD,
                          nullptr, PRIO_LOW,    &hTaskDashboard,  0);
}

// =============================================================================
// logf — printf thread-safe; toma mtxSerial para evitar logs entrelazados.
// =============================================================================
void logf(const char* fmt, ...) {
  if (mtxSerial && xSemaphoreTake(mtxSerial, pdMS_TO_TICKS(50)) == pdTRUE) {
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
    xSemaphoreGive(mtxSerial);
  }
}
