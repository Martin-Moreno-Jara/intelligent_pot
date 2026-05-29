// =============================================================================
// irrigation.cpp — FSM de riego por pulsos:
//   IDLE -> PULSE_ON -> SETTLE -> (PULSE_ON | COOLDOWN) -> ... -> IDLE
//
// Garantías de seguridad:
//   * pumpOff() se llama antes de pinMode en irrigation_init() para que el
//     driver de salida arranque con el nivel seguro (HIGH con PUMP_ACTIVE_LOW).
//   * Hysteresis en IDLE: solo cuenta NUEVAS lecturas del sensor (guard por
//     timestampMs). Sensor period=2s, tarea period=250ms; sin el guard el mismo
//     dato se contaría 8 veces y la hysteresis quedaría inútil.
//   * Se necesitan IRRIGATION_DRY_COUNT lecturas distintas y consecutivas bajo
//     el umbral para arrancar un ciclo → ~6 s de suelo seco real.
//   * Comprobación del umbral DURANTE IRR_PULSE_ON para corte inmediato si el
//     sensor ya alcanzó la humedad objetivo antes de que expire el pulso.
// =============================================================================
#include "irrigation.h"
#include "../core/config.h"
#include "../core/settings.h"

// Número de lecturas consecutivas por debajo del umbral necesarias para
// iniciar riego automático. Sensor period = 2 s → 3 lecturas = 6 s de gracia.
#define IRRIGATION_DRY_COUNT  3

enum IrrState : uint8_t {
  IRR_IDLE,       // monitoreando; bomba apagada
  IRR_PULSE_ON,   // bomba encendida durante un pulso
  IRR_SETTLE,     // bomba apagada, esperando a que el sensor se estabilice
  IRR_COOLDOWN    // pausa obligatoria tras terminar un ciclo
};

static IrrState s_state         = IRR_IDLE;
static uint32_t s_stateStartMs  = 0;     // millis() al entrar al estado actual
static uint8_t  s_pulseCount    = 0;     // pulsos dados en el ciclo en curso
static uint8_t  s_dryCount      = 0;     // lecturas consecutivas bajo el umbral
static uint32_t s_lastSensorTs  = 0;     // timestampMs del último snap ya contado

// -----------------------------------------------------------------------------
// pumpOn/pumpOff — respetan la polaridad configurada (optoacoplador o MOSFET).
// -----------------------------------------------------------------------------
static inline void pumpOn() {
#if PUMP_ACTIVE_LOW
  digitalWrite(PIN_PUMP, LOW);
#else
  digitalWrite(PIN_PUMP, HIGH);
#endif
}
static inline void pumpOff() {
#if PUMP_ACTIVE_LOW
  digitalWrite(PIN_PUMP, HIGH);
#else
  digitalWrite(PIN_PUMP, LOW);
#endif
}

static inline void enterState(IrrState st) {
  s_state        = st;
  s_stateStartMs = millis();
}

// -----------------------------------------------------------------------------
// beginCycle — arranca un ciclo: primer pulso + canción configurada.
// -----------------------------------------------------------------------------
static void beginCycle() {
  s_dryCount    = 0;
  s_lastSensorTs = 0;
  logf("[irrigation] inicio de ciclo");
  xEventGroupSetBits(evtSystem, EVT_IRRIGATING);

  RuntimeSettings st;
  settings_get(st);
  if (st.wateringSongIndex >= 0) {
    int cmd = st.wateringSongIndex;
    xQueueSend(qAudioCmd, &cmd, 0);
  }

  pumpOn();
  s_pulseCount = 1;
  enterState(IRR_PULSE_ON);
}

// -----------------------------------------------------------------------------
// endCycle — termina el ciclo: bomba apagada, entra en COOLDOWN.
// -----------------------------------------------------------------------------
static void endCycle(const char* reason) {
  logf("[irrigation] fin de ciclo (%s)", reason);
  pumpOff();
  xEventGroupClearBits(evtSystem, EVT_IRRIGATING);
  s_dryCount    = 0;
  s_lastSensorTs = 0;
  enterState(IRR_COOLDOWN);
}

// =============================================================================
// irrigation_init — configura el GPIO de la bomba (apagada).
// =============================================================================
bool irrigation_init() {
  // Pre-cargar el registro de salida ANTES de habilitar el driver de output.
  // En ESP32, gpio_set_level funciona antes de gpio_set_direction, así que
  // el nivel ya está fijado cuando el driver se activa y no hay pulso transitorio.
  // Con PUMP_ACTIVE_LOW=1 el nivel seguro es HIGH (bomba apagada).
  pumpOff();
  pinMode(PIN_PUMP, OUTPUT);
  return true;
}

// =============================================================================
// taskIrrigation — Período 250 ms. Procesa comandos manuales y corre la FSM.
// =============================================================================
void taskIrrigation(void* arg) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(250);
  SensorData snap{};

  for (;;) {
    // ---- Comandos manuales (botón del dashboard, MQTT) ----
    IrrigationCmd cmd;
    while (xQueueReceive(qIrrigationCmd, &cmd, 0) == pdTRUE) {
      if (cmd == IRR_CMD_MANUAL_START) {
        if (s_state == IRR_IDLE || s_state == IRR_COOLDOWN) {
          beginCycle();
        }
      } else if (cmd == IRR_CMD_STOP) {
        if (s_state != IRR_IDLE && s_state != IRR_COOLDOWN) {
          endCycle("stop manual");
        }
      }
    }

    bool     haveSnap = (xQueuePeek(qSensorData, &snap, 0) == pdTRUE);
    uint32_t now      = millis();

    RuntimeSettings st;
    settings_get(st);
    float threshold = st.soilThresholdPct;

    switch (s_state) {
      case IRR_IDLE:
        // Solo contar si el snap es NUEVO (timestampMs distinto al último ya procesado).
        // La tarea corre a 250 ms pero el sensor actualiza cada 2000 ms; sin este
        // guard, el mismo dato seco se contaría 8 veces seguidas, anulando la hysteresis.
        if (haveSnap && snap.timestampMs != s_lastSensorTs) {
          s_lastSensorTs = snap.timestampMs;
          if (snap.soilMoisturePct < threshold) {
            s_dryCount++;
            logf("[irrigation] seco %.1f%% < %.1f%% (%u/%u)",
                 snap.soilMoisturePct, threshold, s_dryCount, IRRIGATION_DRY_COUNT);
            if (s_dryCount >= IRRIGATION_DRY_COUNT) {
              beginCycle();
            }
          } else {
            if (s_dryCount > 0) {
              logf("[irrigation] humedad ok %.1f%% >= %.1f%%, contador reseteado",
                   snap.soilMoisturePct, threshold);
              s_dryCount = 0;
            }
          }
        }
        break;

      case IRR_PULSE_ON:
        // Comprobar el umbral EN CADA TICK durante el pulso: si el sensor ya
        // alcanzó la humedad objetivo, cortar la bomba inmediatamente.
        if (haveSnap && snap.soilMoisturePct >= threshold) {
          logf("[irrigation] umbral alcanzado durante pulso (%.1f%% >= %.1f%%)",
               snap.soilMoisturePct, threshold);
          endCycle("umbral alcanzado en pulso");
        } else if ((now - s_stateStartMs) >= IRRIGATION_PULSE_MS) {
          pumpOff();
          logf("[irrigation] pulso %u completado -> espera", s_pulseCount);
          enterState(IRR_SETTLE);
        }
        break;

      case IRR_SETTLE:
        if ((now - s_stateStartMs) >= IRRIGATION_SETTLE_MS) {
          if (haveSnap && snap.soilMoisturePct >= threshold) {
            endCycle("umbral alcanzado tras espera");
          } else if (s_pulseCount >= IRRIGATION_MAX_PULSES) {
            endCycle("max pulsos");
          } else {
            pumpOn();
            s_pulseCount++;
            logf("[irrigation] aún seco (%.1f%% < %.1f%%) -> pulso %u",
                 haveSnap ? snap.soilMoisturePct : -1.0f, threshold, s_pulseCount);
            enterState(IRR_PULSE_ON);
          }
        }
        break;

      case IRR_COOLDOWN:
        if ((now - s_stateStartMs) >= IRRIGATION_COOLDOWN_MS) {
          s_dryCount = 0;
          enterState(IRR_IDLE);
          logf("[irrigation] cooldown terminado");
        }
        break;
    }

    vTaskDelayUntil(&lastWake, period);
  }
}
