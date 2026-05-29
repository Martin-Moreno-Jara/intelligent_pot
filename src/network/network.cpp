// =============================================================================
// network.cpp — WiFi (portal cautivo + NVS) + MQTT a ThingSpeak.
//
// Flujo de conexión:
//   1. enableHighPerfMode(): WiFi.setSleep(WIFI_PS_NONE) + TX power al máximo.
//   2. loadCredentials() desde Preferences (NVS, namespace WIFI_NVS_NAMESPACE).
//   3. Si hay credenciales -> tryConnect() con timeout. Si funcionan, listo.
//   4. Si no -> runProvisioningPortal(): AP "Matera-Setup" + DNS hijack +
//      WebServer en :80 con form de SSID/password. Al recibir, intenta
//      conectar; si falla vuelve al portal. Al éxito guarda en NVS y apaga AP.
//
// MQTT:
//   * Cliente PubSubClient apuntando a mqtt3.thingspeak.com:1883.
//   * Publica en channels/<CH>/publish cada MQTT_PUBLISH_PERIOD_MS.
//   * Se suscribe a channels/<CH>/subscribe/fields/field<TS_FIELD_CMD_PLAY>
//     y .../field<TS_FIELD_CMD_WATER> para recibir comandos remotos.
// =============================================================================
#include "network.h"
#include "../core/config.h"
#include "../core/settings.h"
#include "../storage/sd_store.h"
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>

// Helpers para concatenar un #define numérico dentro de un string literal
// (necesario porque TS_FIELD_CMD_PLAY/WATER son macros).
#define STR_HELPER(x) #x
#define STR(x)        STR_HELPER(x)

static WiFiClient    s_wifiClient;
static PubSubClient  s_mqtt(s_wifiClient);
static Preferences   s_prefs;

// -----------------------------------------------------------------------------
// enableHighPerfMode — desactiva power-save y sube TX al máximo. Llamar
// SIEMPRE antes de scan / WiFi.begin para que los ACKs alcancen al AP en
// entornos saturados.
// -----------------------------------------------------------------------------
static void enableHighPerfMode() {
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_TX_POWER);
}

// -----------------------------------------------------------------------------
// loadCredentials / saveCredentials — persistencia en NVS (Preferences).
// -----------------------------------------------------------------------------
static bool loadCredentials(String& ssid, String& pass) {
  s_prefs.begin(WIFI_NVS_NAMESPACE, /*readOnly=*/true);
  ssid = s_prefs.getString(WIFI_NVS_KEY_SSID, "");
  pass = s_prefs.getString(WIFI_NVS_KEY_PASS, "");
  s_prefs.end();
  return ssid.length() > 0;
}

static void saveCredentials(const String& ssid, const String& pass) {
  s_prefs.begin(WIFI_NVS_NAMESPACE, /*readOnly=*/false);
  s_prefs.putString(WIFI_NVS_KEY_SSID, ssid);
  s_prefs.putString(WIFI_NVS_KEY_PASS, pass);
  s_prefs.end();
  logf("[wifi] credenciales persistidas en NVS (ssid=%s)", ssid.c_str());
}

// -----------------------------------------------------------------------------
// tryConnect — intenta STA connect con timeout configurado. Aplica el modo
// de alto rendimiento antes del begin.
// -----------------------------------------------------------------------------
static bool tryConnect(const String& ssid, const String& pass,
                       uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  enableHighPerfMode();
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < timeoutMs) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

// -----------------------------------------------------------------------------
// buildPortalPage — HTML del portal. Lista las redes detectadas con su RSSI
// para que el usuario elija la mejor.
// -----------------------------------------------------------------------------
static String buildPortalPage() {
  String html;
  html.reserve(2048);
  html  = F("<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Matera WiFi</title><style>"
            "body{font-family:sans-serif;margin:20px;max-width:480px}"
            "h2{color:#2a9}"
            "select,input{width:100%;padding:8px;margin:6px 0;"
              "box-sizing:border-box;font-size:16px}"
            "button{padding:12px 20px;background:#2a9;color:#fff;border:0;"
              "font-size:16px;width:100%}"
            "a{color:#2a9}</style></head><body>"
            "<h2>Matera inteligente</h2>"
            "<p>Seleccioná tu red WiFi e ingresá la contraseña:</p>"
            "<form method='POST' action='/save'>"
            "<select name='ssid'>");
  int n = WiFi.scanComplete();
  if (n <= 0) {
    html += F("<option value=''>(escaneando...)</option>");
  } else {
    for (int i = 0; i < n; ++i) {
      html += "<option value='" + WiFi.SSID(i) + "'>"
            + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
  }
  html += F("</select>"
            "<input type='password' name='pass' placeholder='Contraseña' autocomplete='off'>"
            "<button type='submit'>Conectar</button></form>"
            "<p><a href='/rescan'>Re-escanear redes</a></p>"
            "</body></html>");
  return html;
}

// -----------------------------------------------------------------------------
// runProvisioningPortal — levanta AP + DNS + HTTP y bloquea hasta que el
// usuario provee credenciales válidas. Al retornar, ssidOut/passOut tienen
// las credenciales y la STA está conectada.
// -----------------------------------------------------------------------------
static void runProvisioningPortal(String& ssidOut, String& passOut) {
  WiFi.mode(WIFI_AP_STA);
  enableHighPerfMode();
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress apIp = WiFi.softAPIP();

  DNSServer dns;
  dns.start(/*port=*/53, "*", apIp);   // hijack: cualquier dominio -> apIp

  WebServer server(80);

  bool   submitted = false;
  String formSsid, formPass;

  server.on("/", HTTP_GET, [&]() {
    server.send(200, "text/html", buildPortalPage());
  });

  server.on("/rescan", HTTP_GET, [&]() {
    WiFi.scanDelete();
    WiFi.scanNetworks(/*async=*/true);
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.on("/save", HTTP_POST, [&]() {
    formSsid = server.arg("ssid");
    formPass = server.arg("pass");
    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;margin:20px'>"
      "<h3>Conectando a " + formSsid + "...</h3>"
      "<p>El hotspot se desactivará si la conexión tiene éxito.</p>"
      "</body></html>");
    submitted = true;
  });

  // Respuestas iOS/Android para que el SO detecte el portal cautivo y abra
  // el navegador automáticamente al conectarse al AP.
  server.onNotFound([&]() {
    server.sendHeader("Location", "http://" + apIp.toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  WiFi.scanNetworks(/*async=*/true);
  logf("[wifi] portal activo. AP=%s  IP=%s",
       AP_SSID, apIp.toString().c_str());

  for (;;) {
    dns.processNextRequest();
    server.handleClient();

    if (submitted) {
      logf("[wifi] probando credenciales SSID=%s", formSsid.c_str());

      // Apagar AP momentáneamente y probar STA pura: maximiza chances de
      // asociación en entornos saturados.
      WiFi.softAPdisconnect(true);

      if (tryConnect(formSsid, formPass, WIFI_CONNECT_TIMEOUT_MS)) {
        ssidOut = formSsid;
        passOut = formPass;
        break;
      }

      // Falló -> reabrir el portal con un mensaje.
      logf("[wifi] credenciales inválidas, reabriendo portal");
      submitted = false;
      WiFi.mode(WIFI_AP_STA);
      enableHighPerfMode();
      WiFi.softAP(AP_SSID, AP_PASSWORD);
      WiFi.scanDelete();
      WiFi.scanNetworks(true);
    }
    delay(10);  // ceder CPU; estamos antes de que arranque FreeRTOS scheduler
  }

  server.stop();
  dns.stop();
  WiFi.mode(WIFI_STA);          // apaga AP, sólo STA
  enableHighPerfMode();
  logf("[wifi] portal cerrado. STA IP=%s",
       WiFi.localIP().toString().c_str());
}

// =============================================================================
// network_initWiFi — entry point del aprovisionamiento.
// =============================================================================
bool network_initWiFi() {
  String ssid, pass;
  if (loadCredentials(ssid, pass)) {
    logf("[wifi] NVS tiene SSID=%s, intentando conexión directa", ssid.c_str());
    if (tryConnect(ssid, pass, WIFI_CONNECT_TIMEOUT_MS)) {
      xEventGroupSetBits(evtSystem, EVT_WIFI_CONNECTED);
      logf("[wifi] conectado. IP=%s",
           WiFi.localIP().toString().c_str());
      return true;
    }
    logf("[wifi] credenciales NVS no funcionan -> portal");
  } else {
    logf("[wifi] NVS vacío -> portal de aprovisionamiento");
  }

  runProvisioningPortal(ssid, pass);
  saveCredentials(ssid, pass);
  xEventGroupSetBits(evtSystem, EVT_WIFI_CONNECTED);
  return true;
}

// =============================================================================
// MQTT — ThingSpeak es el único broker.
// =============================================================================

// Topics dinámicos basados en THINGSPEAK_CHANNEL_ID. Se construyen una sola
// vez al arrancar la tarea y se reutilizan.
static char s_topicPub[64];
static char s_topicSubPlay[80];
static char s_topicSubWater[80];

// -----------------------------------------------------------------------------
// onMqttMessage — un único callback para los dos topics suscritos.
// El protocolo ThingSpeak entrega el último valor del field como payload.
// "1" / cualquier valor != 0 -> ejecutar comando; "0" -> stop (sólo riego).
// -----------------------------------------------------------------------------
static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  char buf[32] = {0};
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  logf("[mqtt] rx topic=%s payload=%s", topic, buf);

  if (strcmp(topic, s_topicSubPlay) == 0) {
    if (atoi(buf) != 0) {
      // Reproducir la canción de riego configurada; si no hay, la primera de
      // la SD (si existe alguna).
      RuntimeSettings st;
      settings_get(st);
      int idx = (st.wateringSongIndex >= 0) ? st.wateringSongIndex
                                            : (sd_songCount() > 0 ? 0 : -1);
      if (idx >= 0) xQueueSend(qAudioCmd, &idx, 0);
    }
  } else if (strcmp(topic, s_topicSubWater) == 0) {
    IrrigationCmd cmd = (atoi(buf) != 0)
        ? IRR_CMD_MANUAL_START
        : IRR_CMD_STOP;
    xQueueSend(qIrrigationCmd, &cmd, 0);
  }
}

// -----------------------------------------------------------------------------
// mqttReconnect — connect con credenciales MQTT de ThingSpeak (3-tuple).
// -----------------------------------------------------------------------------
static bool mqttReconnect() {
  if (s_mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  bool ok = s_mqtt.connect(THINGSPEAK_MQTT_CLIENT_ID,
                           THINGSPEAK_MQTT_USER,
                           THINGSPEAK_MQTT_PASS);
  if (ok) {
    s_mqtt.subscribe(s_topicSubPlay);
    s_mqtt.subscribe(s_topicSubWater);
    xEventGroupSetBits(evtSystem, EVT_MQTT_CONNECTED);
    logf("[mqtt] conectado a ThingSpeak. sub: %s , %s",
         s_topicSubPlay, s_topicSubWater);
  } else {
    xEventGroupClearBits(evtSystem, EVT_MQTT_CONNECTED);
    logf("[mqtt] connect FAILED rc=%d", s_mqtt.state());
  }
  return ok;
}

// -----------------------------------------------------------------------------
// publishToThingSpeak — publica un channel update en formato form-encoded.
// El topic es channels/<CH>/publish y el broker mapea field1..6 al canal.
// -----------------------------------------------------------------------------
static void publishToThingSpeak(const SensorData& s) {
  char payload[224];
  snprintf(payload, sizeof(payload),
    "field" STR(TS_FIELD_SOIL) "=%.2f"
    "&field" STR(TS_FIELD_TEMP) "=%.2f"
    "&field" STR(TS_FIELD_HUM)  "=%.2f"
    "&field" STR(TS_FIELD_LUX)  "=%.1f"
    "&field" STR(TS_FIELD_PPM)  "=%.0f"
    "&status=irr=%d",
    s.soilMoisturePct, s.tempC, s.humPct,
    s.lux, s.airQualityPpm,
    (xEventGroupGetBits(evtSystem) & EVT_IRRIGATING) ? 1 : 0);

  s_mqtt.publish(s_topicPub, payload);
  logf("[mqtt] pub %s -> %s", s_topicPub, payload);
}

// =============================================================================
// taskMQTT — mantiene la conexión, publica cada MQTT_PUBLISH_PERIOD_MS y
// procesa subs entrantes. Período de loop 200 ms (necesario para PubSubClient).
// =============================================================================
void taskMQTT(void* arg) {
  // Topics dependen del channel id (macro), se construyen una vez.
  snprintf(s_topicPub, sizeof(s_topicPub),
           "channels/%lu/publish", (unsigned long)THINGSPEAK_CHANNEL_ID);
  snprintf(s_topicSubPlay, sizeof(s_topicSubPlay),
           "channels/%lu/subscribe/fields/field" STR(TS_FIELD_CMD_PLAY),
           (unsigned long)THINGSPEAK_CHANNEL_ID);
  snprintf(s_topicSubWater, sizeof(s_topicSubWater),
           "channels/%lu/subscribe/fields/field" STR(TS_FIELD_CMD_WATER),
           (unsigned long)THINGSPEAK_CHANNEL_ID);

  s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
  s_mqtt.setCallback(onMqttMessage);
  s_mqtt.setBufferSize(512);

  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t loopPeriod = pdMS_TO_TICKS(200);
  uint32_t lastPublish = 0;
  SensorData snap{};

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      xEventGroupClearBits(evtSystem,
                           EVT_WIFI_CONNECTED | EVT_MQTT_CONNECTED);
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
          publishToThingSpeak(snap);
        }
        lastPublish = now;
      }
    }
    vTaskDelayUntil(&lastWake, loopPeriod);
  }
}
