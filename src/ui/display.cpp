// =============================================================================
// display.cpp — Renderiza páginas rotativas en una TFT a color 1.3" (ST7789,
// 240x240, SPI).
//
// Páginas:
//   0: Resumen (humedad suelo + estado riego/red)
//   1: Ambiente (T, H, P)
//   2: Luz + AQI
//   3: Red local (IP, URL del dashboard, estado WiFi/MQTT)
//
// Diseño:
//   * Para evitar parpadeo redibujamos sólo el área de contenido (debajo de
//     la barra superior) en cada cambio de página/valor, en lugar de borrar
//     toda la pantalla en cada tick.
//   * Mantenemos un último-snapshot renderizado por página para repintar
//     únicamente cuando hay cambios significativos.
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
// el HSPI queda libre para la microSD del audio. CS y backlight ahora SÍ están
// cableados (pines 15 y 16). El constructor sin SPIClass usa el objeto global SPI.
static Adafruit_ST7789 tft(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);
static bool s_ready = false;

// Paleta — colores RGB565 fijos para el tema. Usar #define evita instanciar
// objetos antes de que ST7789 esté inicializado.
#define COL_BG         ST77XX_BLACK
#define COL_FG         ST77XX_WHITE
#define COL_ACCENT     0x05B3    // verde-azulado (#0AB69A aprox.) en RGB565
#define COL_MUTED      0x7BEF    // gris medio
#define COL_OK         ST77XX_GREEN
#define COL_BAD        ST77XX_RED
#define COL_WARN       ST77XX_YELLOW

// Layout: barra de estado arriba, área de contenido debajo.
static const int16_t HDR_H        = 28;
static const int16_t CONTENT_Y    = HDR_H + 2;
static const int16_t CONTENT_H    = TFT_HEIGHT - CONTENT_Y;

// =============================================================================
// display_init — inicia SPI dedicado y arranca el ST7789. Llamar después de
// Serial.begin pero no requiere I2C ni mutex.
// =============================================================================
bool display_init() {
  // Backlight encendido (BL = pin 16).
  if (TFT_PIN_BLK >= 0) {
    pinMode(TFT_PIN_BLK, OUTPUT);
    digitalWrite(TFT_PIN_BLK, HIGH);
  }

  // Inicia el bus SPI por defecto con los pines del TFT (igual que el test:
  // SPI.begin(SCL, -1, SDA, CS)).
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
// drawHeader — barra superior con estados WiFi/MQTT/Riego. Se repinta cuando
// alguno de los bits cambia.
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
// clearContent — limpia sólo el área debajo del header. Evita el flash de
// pantalla completa.
// -----------------------------------------------------------------------------
static void clearContent() {
  tft.fillRect(0, CONTENT_Y, TFT_WIDTH, CONTENT_H, COL_BG);
}

// -----------------------------------------------------------------------------
// drawKVLine — dibuja "label: value unit" en una línea, con color para el
// valor. y es la coordenada Y absoluta.
// -----------------------------------------------------------------------------
static void drawKVLine(int16_t y, const char* label,
                       const String& value, const char* unit,
                       uint16_t valueColor) {
  tft.setTextSize(2);
  tft.setCursor(10, y);
  tft.setTextColor(COL_MUTED);
  tft.print(label);
  tft.setTextColor(valueColor);
  tft.print(value);
  if (unit && *unit) {
    tft.setTextColor(COL_MUTED);
    tft.print(" ");
    tft.print(unit);
  }
}

// -----------------------------------------------------------------------------
// renderPage — repinta el contenido completo de la página indicada. Se llama
// cuando cambia la página o cuando hubo cambios apreciables en los datos.
// -----------------------------------------------------------------------------
static void renderPage(uint8_t page, const SensorData& s, bool haveData,
                       bool irrigating) {
  clearContent();

  if (!haveData) {
    tft.setTextSize(2);
    tft.setCursor(10, CONTENT_Y + 30);
    tft.setTextColor(COL_MUTED);
    tft.print("Esperando");
    tft.setCursor(10, CONTENT_Y + 60);
    tft.print("sensores...");
    return;
  }

  switch (page) {
    case 0: {
      tft.setTextSize(2);
      tft.setCursor(10, CONTENT_Y + 4);
      tft.setTextColor(COL_MUTED);
      tft.print("Humedad del suelo");

      tft.setTextSize(6);
      tft.setCursor(10, CONTENT_Y + 30);
      // Color según el umbral vigente (ajustable desde el dashboard): rojo por
      // debajo, amarillo en el margen justo encima, verde cuando hay holgura.
      RuntimeSettings rs;
      settings_get(rs);
      uint16_t col = COL_OK;
      if (s.soilMoisturePct < rs.soilThresholdPct)            col = COL_BAD;
      else if (s.soilMoisturePct < rs.soilThresholdPct + 15)  col = COL_WARN;
      tft.setTextColor(col);
      tft.print((int)s.soilMoisturePct);
      tft.setTextSize(3);
      tft.print("%");

      tft.setTextSize(2);
      tft.setCursor(10, CONTENT_Y + 110);
      tft.setTextColor(COL_MUTED);
      tft.print("Estado: ");
      tft.setTextColor(irrigating ? COL_WARN : COL_FG);
      tft.print(irrigating ? "REGANDO" : "Reposo");

      tft.setTextSize(1);
      tft.setCursor(10, CONTENT_Y + 145);
      tft.setTextColor(COL_MUTED);
      tft.print("ADC: ");
      tft.print(s.soilRaw);
      break;
    }
    case 1: {
      tft.setTextSize(2);
      tft.setCursor(10, CONTENT_Y + 4);
      tft.setTextColor(COL_ACCENT);
      tft.print("Ambiente");

      drawKVLine(CONTENT_Y + 36, "Temp ",
                 String(s.tempC, 1),  "C", COL_FG);
      drawKVLine(CONTENT_Y + 72, "Hum  ",
                 String(s.humPct, 1), "%", COL_FG);
      break;
    }
    case 2: {
      tft.setTextSize(2);
      tft.setCursor(10, CONTENT_Y + 4);
      tft.setTextColor(COL_ACCENT);
      tft.print("Luz / Aire");

      drawKVLine(CONTENT_Y + 36, "Lux  ",
                 String(s.lux, 0),           "lx",  COL_FG);
      drawKVLine(CONTENT_Y + 72, "Aire ",
                 String(s.airQualityPpm, 0), "ppm", COL_FG);

      const char* qual = "Bueno";
      uint16_t qcol = COL_OK;
      if (s.airQualityPpm > 1000) { qual = "Malo";    qcol = COL_BAD; }
      else if (s.airQualityPpm > 700) { qual = "Regular"; qcol = COL_WARN; }
      drawKVLine(CONTENT_Y + 108, "AQI  ", qual, "", qcol);
      break;
    }
    case 3: {
      tft.setTextSize(2);
      tft.setCursor(10, CONTENT_Y + 4);
      tft.setTextColor(COL_ACCENT);
      tft.print("Red local");

      bool wifiOk = WiFi.status() == WL_CONNECTED;

      tft.setTextSize(1);
      tft.setCursor(10, CONTENT_Y + 36);
      tft.setTextColor(COL_MUTED);
      tft.print("IP asignada");

      tft.setTextSize(2);
      tft.setCursor(10, CONTENT_Y + 52);
      tft.setTextColor(wifiOk ? COL_FG : COL_BAD);
      tft.print(wifiOk ? WiFi.localIP().toString() : String("--"));

      tft.setTextSize(1);
      tft.setCursor(10, CONTENT_Y + 84);
      tft.setTextColor(COL_MUTED);
      tft.print("Broker MQTT");

      tft.setTextSize(1);
      tft.setCursor(10, CONTENT_Y + 100);
      tft.setTextColor(wifiOk ? COL_ACCENT : COL_MUTED);
      tft.print(MQTT_HOST);

      tft.setTextSize(1);
      tft.setCursor(10, CONTENT_Y + 140);
      tft.setTextColor(COL_MUTED);
      tft.print("SSID: ");
      tft.print(wifiOk ? WiFi.SSID() : String("--"));
      tft.setCursor(10, CONTENT_Y + 154);
      tft.print("RSSI: ");
      if (wifiOk) { tft.print(WiFi.RSSI()); tft.print(" dBm"); }
      else        { tft.print("--"); }
      break;
    }
  }
}

// =============================================================================
// taskDisplay — Período 250 ms para refrescar header con baja latencia. La
// página completa se repinta sólo al cambiar de página o cuando se detecta
// la primera muestra válida (evita parpadeo). El header se repinta cuando
// cambia algún estado.
// =============================================================================
void taskDisplay(void* arg) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(250);

  uint8_t  page = 0;
  uint32_t lastPageChange = 0;
  bool     firstRender    = true;

  // Estados anteriores para repintar sólo cuando cambian.
  bool prevWifi = false, prevMqtt = false, prevIrr = false;
  bool prevHaveSnap = false;
  uint8_t prevPage = 0xFF;

  // Página IP: si estamos mostrándola y la IP cambia (reconexión), forzar
  // repaint sin esperar al cambio de página.
  IPAddress prevIp;

  SensorData snap{};

  for (;;) {
    bool haveSnap = (xQueuePeek(qSensorData, &snap, 0) == pdTRUE);
    EventBits_t bits = xEventGroupGetBits(evtSystem);
    bool wifiOk    = bits & EVT_WIFI_CONNECTED;
    bool mqttOk    = bits & EVT_MQTT_CONNECTED;
    bool irrigating = bits & EVT_IRRIGATING;

    uint32_t now = millis();
    if (now - lastPageChange > DISPLAY_PAGE_PERIOD_MS) {
      page = (page + 1) % 4;
      lastPageChange = now;
    }

    if (!s_ready) {
      vTaskDelayUntil(&lastWake, period);
      continue;
    }

    // Repintar header si cambió cualquier estado, o al primer ciclo.
    if (firstRender ||
        wifiOk != prevWifi || mqttOk != prevMqtt || irrigating != prevIrr) {
      drawHeader(wifiOk, mqttOk, irrigating);
      prevWifi = wifiOk; prevMqtt = mqttOk; prevIrr = irrigating;
    }

    // Repintar contenido cuando cambia la página, cuando aparece la primera
    // lectura tras esperar, o cuando estamos en la página de red y la IP
    // cambió.
    bool ipChanged = (page == 3) && wifiOk && (WiFi.localIP() != prevIp);
    if (firstRender || page != prevPage || haveSnap != prevHaveSnap ||
        ipChanged) {
      renderPage(page, snap, haveSnap, irrigating);
      prevPage = page;
      prevHaveSnap = haveSnap;
      if (wifiOk) prevIp = WiFi.localIP();
      firstRender = false;
    } else if (page < 3) {
      // En páginas de sensores, refrescar los números cada tick sin borrar
      // toda la pantalla: simplemente sobreescribimos esa página.
      renderPage(page, snap, haveSnap, irrigating);
    }

    vTaskDelayUntil(&lastWake, period);
  }
}
