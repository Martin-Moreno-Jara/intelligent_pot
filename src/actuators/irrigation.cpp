// irrigation.cpp — Riego por intervalo de tiempo + umbral de humedad.
//
// El ciclo automático se cumple SOLO si se dan ambas condiciones:
//   1. Ha transcurrido irrigationIntervalHrs desde el último ciclo (o boot).
//   2. La humedad del suelo está por debajo de soilThresholdLowPct.
// Si la condición 2 no se cumple al vencer el intervalo, se espera al
// siguiente intervalo sin regar.
// El pulso de bomba dura exactamente IRRIGATION_PULSE_MS (5 s).
//
// El riego manual (broker "1") ejecuta el mismo pulso de 5 s inmediatamente,
// sin afectar el temporizador automático.
#include "irrigation.h"
#include "../core/config.h"
#include "../core/settings.h"

enum IrrState : uint8_t { IRR_WAITING, IRR_PUMPING };

static IrrState  s_state       = IRR_WAITING;
static uint32_t  s_stateStart  = 0;   // millis() al entrar al estado actual
static uint32_t  s_lastCheckMs = 0;   // millis() del último chequeo automático

static inline void drv8833On()  { digitalWrite(PIN_PUMP_IN1, HIGH); }
static inline void drv8833Off() { digitalWrite(PIN_PUMP_IN1, LOW);  }

static void enterState(IrrState st) {
  s_state      = st;
  s_stateStart = millis();
}

static void startPump() {
  drv8833On();
  xEventGroupSetBits(evtSystem, EVT_IRRIGATING);
  enterState(IRR_PUMPING);
  logf("[irrigation] bomba ON (5 s)");
}

static void stopPump() {
  drv8833Off();
  xEventGroupClearBits(evtSystem, EVT_IRRIGATING);
  enterState(IRR_WAITING);
  logf("[irrigation] bomba OFF");
}

bool irrigation_init() {
  pinMode(PIN_PUMP_IN1, OUTPUT);
  drv8833Off();
  // Arranca el temporizador desde ahora para que el primer riego automático
  // ocurra después del primer intervalo completo, no inmediatamente.
  s_lastCheckMs = millis();
  return true;
}

void taskIrrigation(void* arg) {
  SensorData snap{};

  for (;;) {
    // Comandos manuales del broker (no afectan el temporizador automático).
    IrrigationCmd cmd;
    while (xQueueReceive(qIrrigationCmd, &cmd, 0) == pdTRUE) {
      if (cmd == IRR_CMD_MANUAL_START && s_state == IRR_WAITING) {
        startPump();
      }
    }

    RuntimeSettings st;
    settings_get(st);
    uint32_t intervalMs = (uint32_t)(st.irrigationIntervalHrs * 3600000.0f);
    uint32_t now = millis();

    switch (s_state) {
      case IRR_WAITING:
        // ¿Ha vencido el intervalo?
        if ((now - s_lastCheckMs) >= intervalMs) {
          s_lastCheckMs = now;   // reiniciar temporizador pase lo que pase
          // Solo regar si el sensor reporta humedad baja.
          if (xQueuePeek(qSensorData, &snap, 0) == pdTRUE &&
              snap.soilMoisturePct < st.soilThresholdLowPct) {
            startPump();
          } else {
            logf("[irrigation] intervalo vencido, suelo OK (%.1f%% >= %.1f%%)",
                 snap.soilMoisturePct, st.soilThresholdLowPct);
          }
        }
        break;

      case IRR_PUMPING:
        if ((now - s_stateStart) >= IRRIGATION_PULSE_MS) {
          stopPump();
        }
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
