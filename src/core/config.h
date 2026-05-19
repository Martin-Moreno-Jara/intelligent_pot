#pragma once
// =============================================================================
// config.h — Definiciones globales: pines, umbrales, credenciales, timings.
// Centraliza la configuración para que ningún módulo "hardcodee" valores.
// =============================================================================

#include <Arduino.h>

// -------- I2C (compartido por AHT20, BMP280, BH1750 y OLED SSD1306) ----------
#define PIN_I2C_SDA           8
#define PIN_I2C_SCL           9
#define I2C_FREQ_HZ           400000

// -------- Sensores analógicos (ADC1 en ESP32-S3) -----------------------------
#define PIN_SOIL_SENSOR       4    // SEN0193 (capacitivo)
#define PIN_MQ135             5    // MQ-135 (CO2-eq / AQI)
#define ADC_RESOLUTION_BITS   12   // 0..4095

// SEN0193: calibración (ajustar tomando lecturas en seco/agua)
#define SOIL_RAW_DRY          3000  // ADC al aire (suelo seco)
#define SOIL_RAW_WET          1200  // ADC sumergido en agua

// -------- Actuadores ---------------------------------------------------------
#define PIN_PUMP_MOSFET       10   // Gate del MOSFET que controla la bomba
#define PIN_SERVO             11
#define SERVO_POS_IDLE        0     // grados — posición de reposo
#define SERVO_POS_WATERING    90    // grados — posición durante riego

// -------- NeoPixels (dos anillos de 8 LEDs cada uno) -------------------------
#define PIN_NEOPIXEL_SOIL     12
#define PIN_NEOPIXEL_ENV      13
#define NEOPIXEL_COUNT        8
#define NEOPIXEL_BRIGHTNESS   60   // 0..255 (limitar consumo)

// -------- Audio I2S (MAX98357) ----------------------------------------------
#define PIN_I2S_BCLK          15
#define PIN_I2S_LRC           16   // WS / LRCLK
#define PIN_I2S_DOUT          17
#define I2S_SAMPLE_RATE       22050
#define I2S_PORT_NUM          0    // I2S_NUM_0

// -------- Display OLED -------------------------------------------------------
#define OLED_WIDTH            128
#define OLED_HEIGHT           64
#define OLED_I2C_ADDR         0x3C
#define OLED_RESET_PIN        -1
#define DISPLAY_PAGE_PERIOD_MS 4000  // rotación de pantallas

// -------- Lógica de riego ----------------------------------------------------
#define SOIL_MOISTURE_TRIGGER_PCT  35.0f   // por debajo de esto -> regar
#define SOIL_MOISTURE_TARGET_PCT   60.0f   // detener cuando se supera
#define IRRIGATION_MAX_MS          8000    // seguridad: nunca regar más que esto
#define IRRIGATION_COOLDOWN_MS     30000   // tiempo mínimo entre riegos

// -------- Conectividad: WiFi -------------------------------------------------
#define WIFI_SSID             "TU_SSID"
#define WIFI_PASSWORD         "TU_PASSWORD"
#define WIFI_CONNECT_TIMEOUT_MS 20000

// -------- MQTT ---------------------------------------------------------------
#define MQTT_HOST             "broker.hivemq.com"
#define MQTT_PORT             1883
#define MQTT_USER             ""
#define MQTT_PASS             ""
#define MQTT_CLIENT_ID        "matera-esp32-s3"
#define MQTT_TOPIC_CMD_PLAY   "cmnd/matera/play"
#define MQTT_TOPIC_CMD_WATER  "cmnd/matera/water"
#define MQTT_TOPIC_STAT       "stat/matera/sensors"
#define MQTT_PUBLISH_PERIOD_MS 5000

// -------- ThingSpeak ---------------------------------------------------------
#define THINGSPEAK_API_KEY    "TU_WRITE_API_KEY"
#define THINGSPEAK_CHANNEL_ID 0000000UL
#define THINGSPEAK_PERIOD_MS  20000   // rate limit free: >=15s

// -------- Tareas FreeRTOS: stack y prioridad ---------------------------------
#define TASK_STACK_SENSORS    4096
#define TASK_STACK_IRRIGATION 3072
#define TASK_STACK_AUDIO      4096
#define TASK_STACK_LED        3072
#define TASK_STACK_DISPLAY    4096
#define TASK_STACK_MQTT       6144
#define TASK_STACK_THINGSPEAK 6144

#define PRIO_LOW              1
#define PRIO_MEDIUM           2
#define PRIO_HIGH             3
