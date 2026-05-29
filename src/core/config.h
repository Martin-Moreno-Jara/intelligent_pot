#pragma once
// =============================================================================
// config.h — Definiciones globales: pines, umbrales, credenciales, timings.
// Centraliza la configuración para que ningún módulo "hardcodee" valores.
//
// IMPORTANTE: el mapa de pines y la elección de librerías replican EXACTAMENTE
// el código de prueba validado en hardware (testV1_music_servo_neopixel.ino):
//   * Display ST7789 por SPI por defecto (FSPI).
//   * Lector microSD por bus HSPI dedicado.
//   * Audio MP3 por I2S (MAX98357A) leído desde la SD con ESP8266Audio.
//   * Servo 360° continuo controlado por LEDC (sin ESP32Servo).
//   * NeoPixels puramente decorativos (animación de color).
// =============================================================================

#include <Arduino.h>

// -------- I2C (compartido por BH1750) ----------------------------------------
// El display NO usa I2C: la pantalla TFT 1.3" ST7789 va por SPI.
#define PIN_I2C_SDA           8
#define PIN_I2C_SCL           9
#define I2C_FREQ_HZ           100000   // 100 kHz: estable con varios sensores

// -------- DHT11 (temperatura y humedad, bus 1-Wire) --------------------------
#define PIN_DHT               5    // Pin de datos del DHT11

// -------- Sensores analógicos (ADC1 en ESP32-S3) -----------------------------
#define PIN_SOIL_SENSOR       4    // SEN0193 (capacitivo) — "SUCHO" en el test
#define PIN_MQ135             3    // MQ-135 (CO2-eq / AQI) — "AIRE" en el test
#define ADC_RESOLUTION_BITS   12   // 0..4095

// SEN0193: calibración (ajustar tomando lecturas en seco/agua)
#define SOIL_RAW_DRY          3000  // ADC al aire (suelo seco)
#define SOIL_RAW_WET          1500  // ADC sumergido en agua

// -------- Actuadores: bomba --------------------------------------------------
// El test maneja la bomba a través de un optoacoplador en colector común:
// la lógica está INVERTIDA (LOW = encendido, HIGH = apagado). Si tu hardware
// usa un MOSFET directo (HIGH = encendido), poné PUMP_ACTIVE_LOW en 0.
#define PIN_PUMP              10   // "PIN_BOMBA" en el test
#define PUMP_ACTIVE_LOW       1    // 1 = optoacoplador (LOW enciende)

// -------- Servo de rotación continua 360° (LEDC, no ESP32Servo) --------------
// Valores de pulso calibrados para 14 bits a 50 Hz (periodo = 20 ms), iguales
// que en el código de prueba.
#define PIN_SERVO             39
#define SERVO_FRQ             50
#define SERVO_RES             14
#define SERVO_MIN_DUTY        410   // ~0.5 ms — velocidad máxima sentido A
#define SERVO_STOP_DUTY       1229  // ~1.5 ms — detenido absoluto
#define SERVO_MAX_DUTY        2048  // ~2.5 ms — velocidad máxima sentido B
// Velocidad de giro continuo en una sola dirección (-100..100; 0 = parada).
// Un valor positivo moderado mantiene la rotación suave y constante.
#define SERVO_SPEED           12

// -------- NeoPixels (dos anillos de 8 LEDs cada uno) -------------------------
// Ya NO son indicadores: muestran una animación de color por defecto. El brillo
// de cada anillo se ajusta de forma independiente desde el dashboard.
#define PIN_NEOPIXEL_1        17    // "PIN_NEO_1" en el test
#define PIN_NEOPIXEL_2        20    // "PIN_NEO_2" en el test
#define NEOPIXEL_COUNT        8
#define NEOPIXEL_BRIGHTNESS   40    // 0..255, brillo inicial de ambos anillos

// -------- Audio I2S (MAX98357A) + microSD (MP3 con ESP8266Audio) -------------
#define PIN_I2S_BCLK          35
#define PIN_I2S_LRC           36    // WS / LRCLK
#define PIN_I2S_DOUT          37

// Lector microSD HW-203 en bus HSPI dedicado (independiente del SPI del TFT).
#define PIN_SD_CS             11
#define PIN_SD_MOSI           12
#define PIN_SD_MISO           13
#define PIN_SD_SCK            14
#define SD_SPI_FREQ_HZ        4000000UL   // 4 MHz, igual que el test
#define SD_MAX_SONGS          32          // tope de MP3 indexados

// Volumen de audio (índice 0..4 -> ganancia de ESP8266Audio). Default = 4.
#define AUDIO_VOLUME_DEFAULT  4
#define AUDIO_VOLUME_MAX      4

// -------- Display TFT a color (ST7789 1.3" 240x240, SPI por defecto) ---------
// El test conecta CS=15 y backlight (BL)=16, y usa el bus SPI por defecto
// (FSPI) — el HSPI queda libre para la microSD.
#define TFT_WIDTH             240
#define TFT_HEIGHT            240
#define TFT_PIN_SCLK          18    // "TFT_SCL" en el test
#define TFT_PIN_MOSI          21    // "TFT_SDA" en el test
#define TFT_PIN_DC            6
#define TFT_PIN_RST           7
#define TFT_PIN_CS            15
#define TFT_PIN_BLK           16    // backlight; HIGH = encendido
#define TFT_SPI_FREQ_HZ       40000000UL  // 40 MHz, seguro con cables cortos
#define TFT_ROTATION          2           // 0..3; ajustar si el texto sale al revés
#define DISPLAY_PAGE_PERIOD_MS 4000        // rotación de pantallas

// -------- Lógica de riego por pulsos ----------------------------------------
// Para evitar sobre-riego (el sensor puede no reflejar el cambio al instante)
// NO se mantiene la bomba encendida hasta alcanzar un objetivo. En su lugar:
//   1. Si la humedad < umbral -> pulso de bomba IRRIGATION_PULSE_MS.
//   2. Apagar la bomba y esperar IRRIGATION_SETTLE_MS para que el agua se
//      absorba y el sensor se estabilice.
//   3. Re-medir: si sigue por debajo del umbral, repetir el pulso; si ya está
//      por encima, terminar y entrar en cooldown.
// El umbral es ajustable en caliente desde el dashboard (ver settings.h); el
// valor de abajo es sólo el inicial.
#define SOIL_MOISTURE_TRIGGER_PCT  35.0f   // umbral inicial: por debajo -> regar
#define IRRIGATION_PULSE_MS        3500    // duración de cada pulso de bomba (3-4 s)
#define IRRIGATION_SETTLE_MS       10000   // espera tras el pulso para re-medir
#define IRRIGATION_MAX_PULSES      6       // tope de pulsos por ciclo (seguridad)
#define IRRIGATION_COOLDOWN_MS     30000   // tiempo mínimo entre ciclos de riego

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

// -------- MQTT: HiveMQ Cloud (TLS) ------------------------------------------
// Crear un cluster gratuito en https://www.hivemq.com/mqtt-cloud-broker/ y un
// usuario MQTT (Access Management). Pegar abajo la URL del cluster, el usuario
// y la contraseña. La conexión es TLS sobre el puerto 8883.
//   * El ESP32 PUBLICA los sensores (JSON) en MQTT_TOPIC_SENSORS.
//   * El ESP32 se SUSCRIBE a MQTT_TOPIC_CMD_PUMP (la Raspi publica "1"/"0" para
//     activar/parar la bomba) y a MQTT_TOPIC_CMD_PLAY (melodía de riego).
// La Raspberry Pi usa LAS MISMAS credenciales y los mismos topics (ver carpeta
// raspi/ en la raíz del proyecto).
#define MQTT_HOST            "xxxxxxxxxxxx.s1.eu.hivemq.cloud"  // <-- tu cluster
#define MQTT_PORT            8883
#define MQTT_USER            "matera"             // <-- usuario MQTT de HiveMQ
#define MQTT_PASS            "CAMBIA_ESTA_CLAVE"  // <-- contraseña MQTT
#define MQTT_CLIENT_ID       "matera-esp32"       // único por cliente conectado

// Topics (deben coincidir con los de la Raspberry Pi):
#define MQTT_TOPIC_SENSORS   "matera/sensors"     // ESP -> publica datos (JSON)
#define MQTT_TOPIC_CMD_PUMP  "matera/cmd/pump"    // Raspi -> "1"=regar, "0"=stop
#define MQTT_TOPIC_CMD_PLAY  "matera/cmd/play"    // Raspi -> "1"=reproducir

// Período entre publicaciones de sensores (HiveMQ no impone rate limit).
#define MQTT_PUBLISH_PERIOD_MS     5000

// -------- Dashboard local (servidor web en el ESP) ---------------------------
// El ESP corre un servidor HTTP en su IP de STA (la IP que le asigne el router)
// para mostrar un dashboard local con las lecturas actuales de los sensores
// y los controles (riego manual, canción de riego, umbral, brillo NeoPixel).
#define WEB_DASHBOARD_PORT         80
#define WEB_DASHBOARD_REFRESH_MS   2000  // intervalo de polling del cliente JS

// -------- Tareas FreeRTOS: stack y prioridad ---------------------------------
#define TASK_STACK_SENSORS    4096
#define TASK_STACK_IRRIGATION 3072
#define TASK_STACK_AUDIO      8192   // ESP8266Audio + decodificador MP3
#define TASK_STACK_SERVO      2048
#define TASK_STACK_LED        3072
#define TASK_STACK_DISPLAY    4096
#define TASK_STACK_MQTT       6144
#define TASK_STACK_DASHBOARD  6144

#define PRIO_LOW              1
#define PRIO_MEDIUM           2
#define PRIO_HIGH             3
#define PRIO_HIGHER           4    // audio: el decodificador MP3 no debe cortarse
