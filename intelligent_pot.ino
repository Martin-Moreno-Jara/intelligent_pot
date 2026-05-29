// =============================================================================
// inteligent_pot.ino — Matera inteligente con estación ambiental (ESP32-S3)
// Punto de entrada Arduino. setup() inicializa hardware y primitivas FreeRTOS
// y lanza todas las tareas; loop() queda vacío porque toda la lógica vive en
// las tareas (vTaskDelay/xTaskDelayUntil, colas, mutex, event group).
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include "src/core/config.h"
#include "src/core/settings.h"
#include "src/core/tasks.h"
#include "src/storage/sd_store.h"
#include "src/sensors/sensors.h"
#include "src/actuators/irrigation.h"
#include "src/actuators/audio.h"
#include "src/actuators/servo360.h"
#include "src/ui/leds.h"
#include "src/ui/display.h"
#include "src/network/network.h"
#include "src/network/web_dashboard.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n===== Matera inteligente boot =====");

  // 1) Primitivas de sincronización + config en caliente ANTES de cualquier
  //    tarea o módulo que las use.
  tasks_initPrimitives();
  settings_init();

  // 2) Inicialización de periféricos.
  //    sensors_init()  arranca Wire (I2C compartido por los sensores).
  //    display_init()  usa el bus SPI por defecto (FSPI) para la TFT ST7789.
  //    sd_init()       monta la microSD en su bus HSPI dedicado y lista los MP3
  //                    (debe ir ANTES de audio: la tarea de audio lee de la SD).
  //    servo_init()    engancha el servo 360° al timer LEDC.
  bool sensorsOk    = sensors_init();
  bool displayOk    = display_init();
  bool sdOk         = sd_init();
  bool irrigationOk = irrigation_init();
  bool servoOk      = servo_init();
  bool ledsOk       = leds_init();
  bool audioOk      = audio_init();

  logf("[boot] sensors:%d display:%d sd:%d irrigation:%d servo:%d leds:%d audio:%d",
       sensorsOk, displayOk, sdOk, irrigationOk, servoOk, ledsOk, audioOk);

  // 3) WiFi: si hay credenciales en NVS las usa; si fallan o no hay,
  //    levanta el portal cautivo en su propio AP y bloquea hasta tener
  //    credenciales válidas. Al retornar, la STA está asociada y tenemos IP.
  network_initWiFi();

  // Imprimir la IP asignada por el router para que el usuario pueda abrir
  // el dashboard local desde su navegador.
  IPAddress ip = WiFi.localIP();
  Serial.print("[net] IP local asignada: ");
  Serial.println(ip);
  Serial.print("[net] Dashboard: http://");
  Serial.print(ip);
  if (WEB_DASHBOARD_PORT != 80) {
    Serial.print(":");
    Serial.print(WEB_DASHBOARD_PORT);
  }
  Serial.println("/");
  logf("[net] STA conectada IP=%s dashboard=http://%s:%u/",
       ip.toString().c_str(), ip.toString().c_str(),
       (unsigned)WEB_DASHBOARD_PORT);

  // 4) Lanzar todas las tareas FreeRTOS (incluye dashboard local + MQTT a
  //    ThingSpeak; ambas conviven sobre la misma asociación WiFi).
  tasks_startAll();

  Serial.println("===== Setup completo, FreeRTOS en marcha =====");
}

void loop() {
  // Toda la lógica vive en las tareas. Mantener loop() libre y delegado.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
