// =============================================================================
// network.cpp — Conectividad: WiFi, MQTT (PubSubClient), ThingSpeak (HTTP GET).
// El JSON publicado por MQTT y los campos enviados a ThingSpeak provienen del
// snapshot leído con xQueuePeek de qSensorData.
// =============================================================================
#include "network.h"
#include "../core/config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

static WiFiClient    s_wifi;
static PubSubClient  s_mqtt(s_wifi);

// -----------------------------------------------------------------------------
// onMqttMessage — callback para topics suscritos.
// cmnd/matera/play  -> enviar AUDIO_CMD_PLAY_ALERT
// cmnd/matera/water -> enviar IRR_CMD_MANUAL_START (o STOP si payload="stop")
// -----------------------------------------------------------------------------
static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  char buf[32] = {0};
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, payload, n);

  logf("[mqtt] rx topic=%s payload=%s", topic, buf);

  if (strcmp(topic, MQTT_TOPIC_CMD_PLAY) == 0) {
    AudioCmd cmd = AUDIO_CMD_PLAY_ALERT;
    xQueueSend(qAudioCmd, &cmd, 0);
  } else if (strcmp(topic, MQTT_TOPIC_CMD_WATER) == 0) {
    IrrigationCmd cmd =
        (strncmp(buf, "stop", 4) == 0) ? IRR_CMD_STOP : IRR_CMD_MANUAL_START;
    xQueueSend(qIrrigationCmd, &cmd, 0);
  }
}

// =============================================================================
// network_initWiFi — modo STA, espera conexión.
// =============================================================================
bool network_initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
  }
  bool ok = WiFi.status() == WL_CONNECTED;
  if (ok) {
    xEventGroupSetBits(evtSystem, EVT_WIFI_CONNECTED);
    logf("[wifi] connected IP=%s", WiFi.localIP().toString().c_str());
  } else {
    logf("[wifi] connection FAILED");
  }
  return ok;
}

// -----------------------------------------------------------------------------
// mqttReconnect — intenta (re)conectar al broker y resuscribe.
// -----------------------------------------------------------------------------
static bool mqttReconnect() {
  if (s_mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  bool ok = (strlen(MQTT_USER) > 0)
    ? s_mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)
    : s_mqtt.connect(MQTT_CLIENT_ID);
  if (ok) {
    s_mqtt.subscribe(MQTT_TOPIC_CMD_PLAY);
    s_mqtt.subscribe(MQTT_TOPIC_CMD_WATER);
    xEventGroupSetBits(evtSystem, EVT_MQTT_CONNECTED);
    logf("[mqtt] connected");
  } else {
    xEventGroupClearBits(evtSystem, EVT_MQTT_CONNECTED);
    logf("[mqtt] connect FAILED rc=%d", s_mqtt.state());
  }
  return ok;
}

// -----------------------------------------------------------------------------
// publishSensors — serializa snapshot a JSON y publica en MQTT_TOPIC_STAT.
// -----------------------------------------------------------------------------
static void publishSensors(const SensorData& s) {
  char json[256];
  snprintf(json, sizeof(json),
    "{\"soil\":%.1f,\"tempC\":%.2f,\"humPct\":%.2f,\"presHPa\":%.2f,"
    "\"lux\":%.1f,\"ppm\":%.0f,\"irr\":%d,\"ts\":%lu}",
    s.soilMoisturePct, s.tempC, s.humPct, s.pressureHPa,
    s.lux, s.airQualityPpm,
    (xEventGroupGetBits(evtSystem) & EVT_IRRIGATING) ? 1 : 0,
    (unsigned long)s.timestampMs);
  s_mqtt.publish(MQTT_TOPIC_STAT, json);
}

// =============================================================================
// taskMQTT — Período 5 s: reconectar si hace falta, publicar snapshot, loop().
// =============================================================================
void taskMQTT(void* arg) {
  s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
  s_mqtt.setCallback(onMqttMessage);
  s_mqtt.setBufferSize(512);

  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t loopPeriod = pdMS_TO_TICKS(200);
  uint32_t lastPublish = 0;
  SensorData snap{};

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      xEventGroupClearBits(evtSystem, EVT_WIFI_CONNECTED | EVT_MQTT_CONNECTED);
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
      xEventGroupSetBits(evtSystem, EVT_WIFI_CONNECTED);
      if (!s_mqtt.connected()) mqttReconnect();
      s_mqtt.loop();

      uint32_t now = millis();
      if (s_mqtt.connected() &&
          (now - lastPublish) >= MQTT_PUBLISH_PERIOD_MS) {
        if (xQueuePeek(qSensorData, &snap, 0) == pdTRUE) {
          publishSensors(snap);
        }
        lastPublish = now;
      }
    }
    vTaskDelayUntil(&lastWake, loopPeriod);
  }
}

// =============================================================================
// taskThingSpeak — Cada 20 s envía HTTP GET con todos los campos.
// Respeta rate limit de la cuenta free (>=15 s).
// =============================================================================
void taskThingSpeak(void* arg) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(THINGSPEAK_PERIOD_MS);
  SensorData snap{};

  for (;;) {
    if (WiFi.status() == WL_CONNECTED &&
        xQueuePeek(qSensorData, &snap, 0) == pdTRUE) {

      char url[320];
      snprintf(url, sizeof(url),
        "http://api.thingspeak.com/update?api_key=%s"
        "&field1=%.2f&field2=%.2f&field3=%.2f&field4=%.2f"
        "&field5=%.1f&field6=%.0f",
        THINGSPEAK_API_KEY,
        snap.soilMoisturePct, snap.tempC, snap.humPct,
        snap.pressureHPa, snap.lux, snap.airQualityPpm);

      HTTPClient http;
      http.begin(url);
      int code = http.GET();
      logf("[thingspeak] HTTP %d", code);
      http.end();
    }
    vTaskDelayUntil(&lastWake, period);
  }
}
