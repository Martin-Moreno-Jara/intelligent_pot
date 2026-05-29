// =============================================================================
// irrigation.cpp — FSM de riego por pulsos:
//   IDLE -> PULSE_ON -> SETTLE -> (PULSE_ON | COOLDOWN) -> ... -> IDLE
//
// Hardware: bomba en PIN_PUMP. La polaridad la fija PUMP_ACTIVE_LOW en config.h
// (1 = optoacoplador, LOW enciende). El servo de rotación continua se maneja en
// servo360.cpp, no aquí.
// =============================================================================
#include "irrigation.h"
#include "../core/config.h"
#include "../core/settings.h"

enum IrrState : uint8_t {
  IRR_IDLE,       // monitoreando; bomba apagada
  IRR_PULSE_ON,   // bomba encendida durante un pulso
  IRR_SETTLE,     // bomba apagada, esperando a que el sensor se estabilice
  IRR_COOLDOWN    // pausa obligatoria tras terminar un ciclo
};

static IrrState s_state         = IRR_IDLE;
static uint32_t s_stateStartMs  = 0;     // millis() al entrar al estado actual
static uint8_t  s_pulseCount     = 0;    // pulsos dados en el ciclo en curso

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
// beginCycle — arranca un ciclo de riego: primer pulso + canción configurada.
// -----------------------------------------------------------------------------
static void beginCycle() {
  logf("[irrigation] inicio de ciclo");
  xEventGroupSetBits(evtSystem, EVT_IRRIGATING);

  // Disparar la canción de riego (si el usuario configuró una en el dashboard).
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
// endCycle — termina el ciclo: bomba apagada, a COOLDOWN.
// -----------------------------------------------------------------------------
static void endCycle(const char* reason) {
  logf("[irrigation] fin de ciclo (%s)", reason);
  pumpOff();
  xEventGroupClearBits(evtSystem, EVT_IRRIGATING);
  enterState(IRR_COOLDOWN);
}

// =============================================================================
// irrigation_init — configura el GPIO de la bomba (apagada).
// =============================================================================
bool irrigation_init() {
  pinMode(PIN_PUMP, OUTPUT);
  pumpOff();
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
        // Riego manual: fuerza un ciclo aunque el sensor esté por encima del
        // umbral (sólo si no hay uno en curso).
        if (s_state == IRR_IDLE || s_state == IRR_COOLDOWN) {
          beginCycle();
        }
      } else if (cmd == IRR_CMD_STOP) {
        if (s_state == IRR_PULSE_ON || s_state == IRR_SETTLE) {
          endCycle("stop manual");
        }
      }
    }

    bool     haveSnap = (xQueuePeek(qSensorData, &snap, 0) == pdTRUE);
    uint32_t now      = millis();

    // Umbral vigente (ajustable desde el dashboard).
    RuntimeSettings st;
    settings_get(st);
    float threshold = st.soilThresholdPct;

    switch (s_state) {
      case IRR_IDLE:
        // Sólo automatizamos si tenemos lectura válida y está por debajo.
        if (haveSnap && snap.soilMoisturePct < threshold) {
          beginCycle();
        }
        break;

      case IRR_PULSE_ON:
        // Mantener el pulso durante IRRIGATION_PULSE_MS y luego apagar.
        if ((now - s_stateStartMs) >= IRRIGATION_PULSE_MS) {
          pumpOff();
          logf("[irrigation] pulso %u completado -> espera", s_pulseCount);
          enterState(IRR_SETTLE);
        }
        break;

      case IRR_SETTLE:
        // Bomba apagada; dejar absorber el agua y re-medir tras la espera.
        if ((now - s_stateStartMs) >= IRRIGATION_SETTLE_MS) {
          if (haveSnap && snap.soilMoisturePct >= threshold) {
            endCycle("umbral alcanzado");
          } else if (s_pulseCount >= IRRIGATION_MAX_PULSES) {
            endCycle("max pulsos");
          } else {
            // Sigue seco: otro pulso.
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
          enterState(IRR_IDLE);
          logf("[irrigation] cooldown terminado");
        }
        break;
    }

    vTaskDelayUntil(&lastWake, period);
  }
}
