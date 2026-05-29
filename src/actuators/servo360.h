#pragma once
// =============================================================================
// servo360.h — Servo de rotación continua 360° controlado por LEDC.
//
// A diferencia de un servo de posición, este gira sin parar en una sola
// dirección a velocidad moderada y constante (SERVO_SPEED en config.h), TODO el
// tiempo — no sólo durante el riego. Se controla por PWM con la API LEDC del
// core ESP32 3.x (ledcAttach / ledcWrite), sin la librería ESP32Servo, igual
// que el código de prueba.
// =============================================================================

#include "../core/tasks.h"

bool servo_init();           // ledcAttach + parada inicial
void taskServo(void* arg);   // mantiene la rotación continua

// Fija velocidad/sentido: -100 (reversa máx) .. 0 (parada) .. 100 (avance máx).
void controlarServo360(int velocidad);
