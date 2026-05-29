// =============================================================================
// servo360.cpp — Control de servo de rotación continua por LEDC.
// =============================================================================
#include "servo360.h"
#include "../core/config.h"

// =============================================================================
// servo_init — engancha el pin al timer LEDC y deja el servo detenido.
// =============================================================================
bool servo_init() {
  // API LEDC del core ESP32 3.x: ledcAttach(pin, frecuencia, resolución_bits).
  if (!ledcAttach(PIN_SERVO, SERVO_FRQ, SERVO_RES)) {
    logf("[servo] ERROR: ledcAttach falló en pin %d", PIN_SERVO);
    return false;
  }
  controlarServo360(0);   // arrancar detenido
  return true;
}

// =============================================================================
// controlarServo360 — traduce velocidad (-100..100) a ancho de pulso (duty).
// =============================================================================
void controlarServo360(int velocidad) {
  if (velocidad < -100) velocidad = -100;
  if (velocidad >  100) velocidad =  100;

  uint32_t duty;
  if (velocidad == 0) {
    duty = SERVO_STOP_DUTY;                                   // parada
  } else if (velocidad > 0) {
    duty = map(velocidad, 0, 100, SERVO_STOP_DUTY, SERVO_MAX_DUTY);  // avance
  } else {
    duty = map(velocidad, -100, 0, SERVO_MIN_DUTY, SERVO_STOP_DUTY); // reversa
  }
  ledcWrite(PIN_SERVO, duty);
}

// =============================================================================
// taskServo — rotación continua y constante en una sola dirección.
// Al ser un servo 360° no hay bucles de incremento: basta fijar la velocidad y
// dejar la tarea en espera pasiva para no consumir CPU.
// =============================================================================
void taskServo(void* arg) {
  controlarServo360(SERVO_SPEED);   // velocidad moderada, sentido único
  for (;;) {
    // Re-afirmar la velocidad periódicamente por robustez y dormir.
    controlarServo360(SERVO_SPEED);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
