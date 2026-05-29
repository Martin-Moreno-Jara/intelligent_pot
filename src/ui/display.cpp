// =============================================================================
// display.cpp — TFT a color 1.3" (ST7789, 240x240, SPI).
//
// UNA SOLA pantalla con TODOS los valores de los sensores (ya no rota entre
// páginas). Diseño:
//   * Barra superior con estado WiFi/MQTT/Riego (se repinta sólo al cambiar).
//   * Una fila por sensor (suelo, temperatura, humedad, luz, aire) más el
//     estado de riego.
// Para evitar parpadeo dibujamos las etiquetas una sola vez y refrescamos
// únicamente el área del VALOR de cada fila cuando su texto o color cambia.
// =============================================================================
#include "display.h"
#include "../core/config.h"
#include "../core/settings.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>

// El TFT usa el bus SPI POR DEFECTO (FSPI), igual que el código de prueba; así
// el HSPI queda libre para la microSD del audio.
static Adafruit_ST7789 tft(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);
static bool s_ready = false;

// Paleta — colores RGB565 fijos para el tema.
#define COL_BG         ST77XX_BLACK
#define COL_FG         ST77XX_WHITE
#define COL_ACCENT     0x05B3    // verde-azulado
#define COL_MUTED      0x7BEF    // gris medio
#define COL_OK         ST77XX_GREEN
#define COL_BAD        ST77XX_RED
#define COL_WARN       ST77XX_YELLOW

// Layout: barra de estado arriba, filas de sensores debajo.
static const int16_t HDR_H     = 28;
static const int16_t CONTENT_Y = HDR_H + 2;

// Filas de sensores (una sola pantalla).
#define ROW_COUNT  6
static const char* const ROW_LABELS[ROW_COUNT] =
  { "Suelo", "Temp", "Hum", "Luz", "Aire", "Riego" };
static const int16_t ROW_Y[ROW_COUNT] = { 40, 72, 104, 136, 168, 200 };
static const int16_t LABEL_X = 8;
static const int16_t VALUE_X = 120;        // inicio del área de valor
static const int16_t VALUE_W = TFT_WIDTH - VALUE_X;
static const int16_t ROW_H   = 18;         // alto del texto a size 2 (+ margen)

// Caché de lo último dibujado por fila para repintar sólo ante cambios.
static String   s_valCache[ROW_COUNT];
static uint16_t s_colCache[ROW_COUNT];
static bool     s_labelsDrawn = false;
static bool     s_waitingShown = false;

// =============================================================================
// display_init — inicia SPI dedicado y arranca el ST7789.
// =============================================================================
bool display_init() {
  if (TFT_PIN_BLK >= 0) {
    pinMode(TFT_PIN_BLK, OUTPUT);
    digitalWrite(TFT_PIN_BLK, HIGH);
  }

  SPI.begin(TFT_PIN_SCLK, /*MISO=*/-1, TFT_PIN_MOSI, TFT_PIN_CS);

  tft.init(TFT_WIDTH, TFT_HEIGHT);
  tft.setSPISpeed(TFT_SPI_FREQ_HZ);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(COL_BG);

  tft.setTextColor(COL_ACCENT);
  tft.setTextSize(2);
  tft.setCursor(10, 90);
  tft.println("Matera");
  tft.setCursor(10, 115);
  tft.println("inteligente");
  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(10, 150);
  tft.println("Booting...");

  s_ready = true;
  return s_ready;
}

// -----------------------------------------------------------------------------
// drawHeader — barra superior con estados WiFi/MQTT/Riego.
// -----------------------------------------------------------------------------
static void drawHeader(bool wifiOk, bool mqttOk, bool irrigating) {
  tft.fillRect(0, 0, TFT_WIDTH, HDR_H, COL_BG);
  tft.drawFastHLine(0, HDR_H, TFT_WIDTH, COL_MUTED);

  tft.setTextSize(1);

  tft.setCursor(6, 6);
  tft.setTextColor(COL_MUTED);
  tft.print("WiFi ");
  tft.setTextColor(wifiOk ? COL_OK : COL_BAD);
  tft.print(wifiOk ? "OK" : "--");

  tft.setCursor(70, 6);
  tft.setTextColor(COL_MUTED);
  tft.print("MQTT ");
  tft.setTextColor(mqttOk ? COL_OK : COL_BAD);
  tft.print(mqttOk ? "OK" : "--");

  tft.setCursor(140, 6);
  tft.setTextColor(COL_MUTED);
  tft.print("RIEGO ");
  tft.setTextColor(irrigating ? COL_WARN : COL_MUTED);
  tft.print(irrigating ? "ON" : "off");

  tft.setCursor(6, 17);
  tft.setTextColor(COL_MUTED);
  tft.print("Matera inteligente");
}

// -----------------------------------------------------------------------------
// drawLabels — dibuja las etiquetas fijas de cada fila (una sola vez).
// -----------------------------------------------------------------------------
static void drawLabels() {
  tft.fillRect(0, CONTENT_Y, TFT_WIDTH, TFT_HEIGHT - CONTENT_Y, COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_MUTED);
  for (int i = 0; i < ROW_COUNT; ++i) {
    tft.setCursor(LABEL_X, ROW_Y[i]);
    tft.print(ROW_LABELS[i]);
  }
  for (int i = 0; i < ROW_COUNT; ++i) { s_valCache[i] = ""; s_colCache[i] = 0; }
}

// -----------------------------------------------------------------------------
// updateValue — refresca el VALOR de una fila sólo si cambió el texto o color.
// -----------------------------------------------------------------------------
static void updateValue(int row, const String& val, uint16_t color) {
  if (val == s_valCache[row] && color == s_colCache[row]) return;
  tft.fillRect(VALUE_X, ROW_Y[row] - 1, VALUE_W, ROW_H, COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(color);
  tft.setCursor(VALUE_X, ROW_Y[row]);
  tft.print(val);
  s_valCache[row] = val;
  s_colCache[row] = color;
}

// -----------------------------------------------------------------------------
// showWaiting — mensaje mientras no llega la primera lectura.
// -----------------------------------------------------------------------------
static void showWaiting() {
  if (s_waitingShown) return;
  tft.fillRect(0, CONTENT_Y, TFT_WIDTH, TFT_HEIGHT - CONTENT_Y, COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(10, CONTENT_Y + 40);
  tft.print("Esperando");
  tft.setCursor(10, CONTENT_Y + 70);
  tft.print("sensores...");
  s_waitingShown = true;
  s_labelsDrawn  = false;   // forzar redibujo de etiquetas al llegar datos
}

// -----------------------------------------------------------------------------
// renderValues — calcula y actualiza el valor de cada fila a partir del snapshot.
// -----------------------------------------------------------------------------
static void renderValues(const SensorData& s, bool irrigating) {
  if (!s_labelsDrawn) { drawLabels(); s_labelsDrawn = true; s_waitingShown = false; }

  RuntimeSettings rs;
  settings_get(rs);

  // Suelo: color según el umbral vigente.
  uint16_t soilCol = COL_OK;
  if (s.soilMoisturePct < rs.soilThresholdPct)           soilCol = COL_BAD;
  else if (s.soilMoisturePct < rs.soilThresholdPct + 15) soilCol = COL_WARN;
  updateValue(0, String((int)s.soilMoisturePct) + " %", soilCol);

  updateValue(1, String(s.tempC, 1) + " C", COL_FG);
  updateValue(2, String(s.humPct, 1) + " %", COL_FG);
  updateValue(3, String(s.lux, 0) + " lx", COL_FG);

  // Aire: color según calidad.
  uint16_t airCol = COL_OK;
  if (s.airQualityPpm > 1000)     airCol = COL_BAD;
  else if (s.airQualityPpm > 700) airCol = COL_WARN;
  updateValue(4, String(s.airQualityPpm, 0) + " ppm", airCol);

  updateValue(5, irrigating ? String("REGANDO") : String("Reposo"),
              irrigating ? COL_WARN : COL_FG);
}

// =============================================================================
// taskDisplay — Período 250 ms. Repinta el header al cambiar de estado y
// refresca los valores de los sensores (sólo los que cambian).
// =============================================================================
void taskDisplay(void* arg) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(250);

  bool firstRender = true;
  bool prevWifi = false, prevMqtt = false, prevIrr = false;

  SensorData snap{};

  for (;;) {
    if (!s_ready) { vTaskDelayUntil(&lastWake, period); continue; }

    bool haveSnap = (xQueuePeek(qSensorData, &snap, 0) == pdTRUE);
    EventBits_t bits = xEventGroupGetBits(evtSystem);
    bool wifiOk     = bits & EVT_WIFI_CONNECTED;
    bool mqttOk     = bits & EVT_MQTT_CONNECTED;
    bool irrigating = bits & EVT_IRRIGATING;

    if (firstRender ||
        wifiOk != prevWifi || mqttOk != prevMqtt || irrigating != prevIrr) {
      drawHeader(wifiOk, mqttOk, irrigating);
      prevWifi = wifiOk; prevMqtt = mqttOk; prevIrr = irrigating;
      firstRender = false;
    }

    if (haveSnap) renderValues(snap, irrigating);
    else          showWaiting();

    vTaskDelayUntil(&lastWake, period);
  }
}
