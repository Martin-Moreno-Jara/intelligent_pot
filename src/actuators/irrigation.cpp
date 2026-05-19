// =============================================================================
// irrigation.cpp — FSM de riego: IDLE -> WATERING -> COOLDOWN -> IDLE
// Hardware: MOSFET en PIN_PUMP_MOSFET (HIGH = on), servo SG90.
// =============================================================================
#include "irrigation.h"
#include "../core/config.h"
#include <ESP32Servo.h>

static Servo s_servo;

enum IrrState : uint8_t {
  IRR_IDLE,
  IRR_WATERING,
  IRR_COOLDOWN
};
static IrrState s_state          = IRR_IDLE;
static uint32_t s_waterStartMs   = 0;
static uint32_t s_cooldownStart  = 0;

// -----------------------------------------------------------------------------
// pumpOn/pumpOff — control del MOSFET de la bomba.
// -----------------------------------------------------------------------------
static inline void pumpOn()  { digitalWrite(PIN_PUMP_MOSFET, HIGH); }
static inline void pumpOff() { digitalWrite(PIN_PUMP_MOSFET, LOW); }

// -----------------------------------------------------------------------------
// servoTo — mueve el servo y espera un instante para que llegue (sólo se
// llama en transiciones, no en el loop principal de la tarea).
// -----------------------------------------------------------------------------
static void servoTo(int deg) {
  s_servo.write(deg);
  vTaskDelay(pdMS_TO_TICKS(300));
}

// -----------------------------------------------------------------------------
// startWatering — Transición IDLE -> WATERING.
// Mueve servo, abre MOSFET, dispara audio, marca EVT_IRRIGATING.
// -----------------------------------------------------------------------------
static void startWatering() {
  logf("[irrigation] START");
  servoTo(SERVO_POS_WATERING);
  pumpOn();
  s_waterStartMs = millis();
  s_state = IRR_WATERING;
  xEventGroupSetBits(evtSystem, EVT_IRRIGATING);

  AudioCmd cmd = AUDIO_CMD_PLAY_WATER;
  xQueueSend(qAudioCmd, &cmd, 0);
}

// -----------------------------------------------------------------------------
// stopWatering — Transición WATERING -> COOLDOWN.
// Cierra MOSFET, regresa servo, detiene audio, limpia EVT_IRRIGATING.
// -----------------------------------------------------------------------------
static void stopWatering() {
  logf("[irrigation] STOP");
  pumpOff();
  servoTo(SERVO_POS_IDLE);
  s_cooldownStart = millis();
  s_state = IRR_COOLDOWN;
  xEventGroupClearBits(evtSystem, EVT_IRRIGATING);

  AudioCmd cmd = AUDIO_CMD_STOP;
  xQueueSend(qAudioCmd, &cmd, 0);
}

// =============================================================================
// irrigation_init — configura GPIO de la bomba y attach del servo.
// =============================================================================
bool irrigation_init() {
  pinMode(PIN_PUMP_MOSFET, OUTPUT);
  pumpOff();
  s_servo.setPeriodHertz(50);
  s_servo.attach(PIN_SERVO, 500, 2400);
  s_servo.write(SERVO_POS_IDLE);
  return true;
}

// =============================================================================
// taskIrrigation — Período 500 ms. FSM + comandos manuales.
// =============================================================================
void taskIrrigation(void* arg) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(500);
  SensorData snap{};

  for (;;) {
    // ---- Procesar comandos manuales (no bloqueante) ----
    IrrigationCmd cmd;
    while (xQueueReceive(qIrrigationCmd, &cmd, 0) == pdTRUE) {
      if (cmd == IRR_CMD_MANUAL_START && s_state == IRR_IDLE) {
        startWatering();
      } else if (cmd == IRR_CMD_STOP && s_state == IRR_WATERING) {
        stopWatering();
      }
    }

    // ---- Leer snapshot de sensores (peek, no destructivo) ----
    bool haveSnap = (xQueuePeek(qSensorData, &snap, 0) == pdTRUE);

    uint32_t now = millis();
    switch (s_state) {
      case IRR_IDLE:
        if (haveSnap &&
            snap.soilMoisturePct < SOIL_MOISTURE_TRIGGER_PCT) {
          startWatering();
        }
        break;

      case IRR_WATERING:
        // Detener por: humedad alcanzada, tiempo máximo, o falta de sensor.
        if (!haveSnap ||
            snap.soilMoisturePct >= SOIL_MOISTURE_TARGET_PCT ||
            (now - s_waterStartMs) >= IRRIGATION_MAX_MS) {
          stopWatering();
        }
        break;

      case IRR_COOLDOWN:
        if ((now - s_cooldownStart) >= IRRIGATION_COOLDOWN_MS) {
          s_state = IRR_IDLE;
          logf("[irrigation] cooldown done");
        }
        break;
    }

    vTaskDelayUntil(&lastWake, period);
  }
}
