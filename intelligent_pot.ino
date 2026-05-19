// =============================================================================
// inteligent_pot.ino — Matera inteligente con estación ambiental (ESP32-S3)
// Punto de entrada Arduino. setup() inicializa hardware y primitivas FreeRTOS
// y lanza todas las tareas; loop() queda vacío porque toda la lógica vive en
// las tareas (vTaskDelay/xTaskDelayUntil, colas, mutex, event group).
// =============================================================================
#include <Arduino.h>
#include "src/core/config.h"
#include "src/core/tasks.h"
#include "src/sensors/sensors.h"
#include "src/actuators/irrigation.h"
#include "src/actuators/audio.h"
#include "src/ui/leds.h"
#include "src/ui/display.h"
#include "src/network/network.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n===== Matera inteligente boot =====");

  // 1) Primitivas de sincronización ANTES que cualquier tarea o módulo que las use.
  tasks_initPrimitives();

  // 2) Inicialización de periféricos.
  //    sensors_init() arranca Wire (I2C compartido por sensores y OLED).
  bool sensorsOk    = sensors_init();
  bool displayOk    = display_init();
  bool irrigationOk = irrigation_init();
  bool ledsOk       = leds_init();
  bool audioOk      = audio_init();

  logf("[boot] sensors:%d display:%d irrigation:%d leds:%d audio:%d",
       sensorsOk, displayOk, irrigationOk, ledsOk, audioOk);

  // 3) WiFi (bloqueante con timeout). Falla -> seguimos en modo offline:
  //    las tareas de red detectarán la desconexión y reintentarán.
  network_initWiFi();

  // 4) Lanzar todas las tareas FreeRTOS.
  tasks_startAll();

  Serial.println("===== Setup completo, FreeRTOS en marcha =====");
}

void loop() {
  // Toda la lógica vive en las tareas. Mantener loop() libre y delegado.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
