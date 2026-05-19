// =============================================================================
// display.cpp — Renderiza páginas rotativas en SSD1306.
// Páginas:
//   0: Resumen (humedad suelo + estado riego/red)
//   1: Ambiente (T, H, P)
//   2: Luz + AQI
// =============================================================================
#include "display.h"
#include "../core/config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
static bool s_ready = false;

// =============================================================================
// display_init — debe llamarse después de Wire.begin (en sensors_init).
// =============================================================================
bool display_init() {
  if (xSemaphoreTake(mtxI2C, pdMS_TO_TICKS(500)) != pdTRUE) return false;
  s_ready = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
  if (s_ready) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("Matera inteligente");
    oled.println("Booting...");
    oled.display();
  }
  xSemaphoreGive(mtxI2C);
  return s_ready;
}

// -----------------------------------------------------------------------------
// renderPage — dibuja una página específica. Asume mtxI2C ya tomado.
// -----------------------------------------------------------------------------
static void renderPage(uint8_t page, const SensorData& s, bool haveData,
                       bool wifiOk, bool mqttOk, bool irrigating) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);

  // Barra superior con estado
  oled.print("WiFi:"); oled.print(wifiOk ? "OK " : "-- ");
  oled.print("MQTT:"); oled.print(mqttOk ? "OK" : "--");
  oled.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

  if (!haveData) {
    oled.setCursor(0, 20);
    oled.println("Esperando sensores...");
    oled.display();
    return;
  }

  switch (page) {
    case 0: {
      oled.setCursor(0, 14);
      oled.setTextSize(2);
      oled.print("Soil ");
      oled.print((int)s.soilMoisturePct);
      oled.print("%");
      oled.setTextSize(1);
      oled.setCursor(0, 40);
      oled.print(irrigating ? "Estado: REGANDO"
                            : "Estado: Reposo");
      oled.setCursor(0, 52);
      oled.print("Raw ADC: ");
      oled.print(s.soilRaw);
      break;
    }
    case 1: {
      oled.setCursor(0, 14);
      oled.print("Temp:  "); oled.print(s.tempC, 1);  oled.println(" C");
      oled.print("Hum:   "); oled.print(s.humPct, 1); oled.println(" %");
      oled.print("Pres:  "); oled.print(s.pressureHPa, 1); oled.println(" hPa");
      break;
    }
    case 2: {
      oled.setCursor(0, 14);
      oled.print("Lux:   "); oled.print(s.lux, 0);  oled.println(" lx");
      oled.print("Aire:  "); oled.print(s.airQualityPpm, 0); oled.println(" ppm");
      const char* qual = "Bueno";
      if (s.airQualityPpm > 1000) qual = "Malo";
      else if (s.airQualityPpm > 700) qual = "Regular";
      oled.print("AQI:   "); oled.println(qual);
      break;
    }
  }

  oled.display();
}

// =============================================================================
// taskDisplay — Período 1 s, rota página cada DISPLAY_PAGE_PERIOD_MS.
// =============================================================================
void taskDisplay(void* arg) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1000);
  uint8_t page = 0;
  uint32_t lastPageChange = 0;
  SensorData snap{};

  for (;;) {
    bool haveSnap = (xQueuePeek(qSensorData, &snap, 0) == pdTRUE);
    EventBits_t bits = xEventGroupGetBits(evtSystem);
    bool wifiOk    = bits & EVT_WIFI_CONNECTED;
    bool mqttOk    = bits & EVT_MQTT_CONNECTED;
    bool irrigating = bits & EVT_IRRIGATING;

    uint32_t now = millis();
    if (now - lastPageChange > DISPLAY_PAGE_PERIOD_MS) {
      page = (page + 1) % 3;
      lastPageChange = now;
    }

    if (s_ready &&
        xSemaphoreTake(mtxI2C, pdMS_TO_TICKS(100)) == pdTRUE) {
      renderPage(page, snap, haveSnap, wifiOk, mqttOk, irrigating);
      xSemaphoreGive(mtxI2C);
    }

    vTaskDelayUntil(&lastWake, period);
  }
}
