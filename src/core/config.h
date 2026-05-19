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

// -------- Conectividad: WiFi (aprovisionamiento por portal cautivo) ---------
// Las credenciales NO están hardcodeadas. Al primer boot (o si las guardadas
// fallan) el ESP32 levanta un AP propio con un portal web; las credenciales
// recibidas se persisten en NVS (Preferences) y se reutilizan en los
// siguientes boots.
#define AP_SSID                  "Matera-Setup"
#define AP_PASSWORD              "matera1234"     // >=8 chars; "" = AP abierto
#define WIFI_CONNECT_TIMEOUT_MS  20000
#define WIFI_NVS_NAMESPACE       "matera"         // Preferences namespace
#define WIFI_NVS_KEY_SSID        "ssid"
#define WIFI_NVS_KEY_PASS        "pass"

// Alto rendimiento para entornos con muchas redes 2.4 GHz:
//   * potencia TX al máximo
//   * power-save desactivado durante scan / autenticación / DHCP
#define WIFI_TX_POWER            WIFI_POWER_19_5dBm   // 19.5 dBm = máximo

// -------- MQTT: ThingSpeak como ÚNICO broker --------------------------------
// Generar un "MQTT Device" en https://thingspeak.com/devices/mqtt y pegar
// abajo las credenciales que ThingSpeak devuelve. El canal asociado recibirá
// los datos por channels/<id>/publish y emitirá comandos por
// channels/<id>/subscribe/fields/fieldN.
#define MQTT_HOST                  "mqtt3.thingspeak.com"
#define MQTT_PORT                  1883
#define THINGSPEAK_MQTT_CLIENT_ID  "PEGAR_CLIENT_ID"
#define THINGSPEAK_MQTT_USER       "PEGAR_USERNAME"
#define THINGSPEAK_MQTT_PASS       "PEGAR_PASSWORD"
#define THINGSPEAK_CHANNEL_ID      0000000UL

// Asignación de campos del canal (1..8). field1..6 = sensores publicados.
// field7..8 = comandos remotos: el ESP32 se suscribe y reacciona cuando
// alguien escribe en ellos desde ThingSpeak / app / API REST.
#define TS_FIELD_SOIL              1
#define TS_FIELD_TEMP              2
#define TS_FIELD_HUM               3
#define TS_FIELD_PRES              4
#define TS_FIELD_LUX               5
#define TS_FIELD_PPM               6
#define TS_FIELD_CMD_PLAY          7    // payload "1" -> reproducir melodía
#define TS_FIELD_CMD_WATER         8    // payload "1" -> regar; "0" -> stop

// Período entre publicaciones (rate limit ThingSpeak free >= 15 s).
#define MQTT_PUBLISH_PERIOD_MS     20000

// -------- Tareas FreeRTOS: stack y prioridad ---------------------------------
#define TASK_STACK_SENSORS    4096
#define TASK_STACK_IRRIGATION 3072
#define TASK_STACK_AUDIO      4096
#define TASK_STACK_LED        3072
#define TASK_STACK_DISPLAY    4096
#define TASK_STACK_MQTT       6144

#define PRIO_LOW              1
#define PRIO_MEDIUM           2
#define PRIO_HIGH             3
