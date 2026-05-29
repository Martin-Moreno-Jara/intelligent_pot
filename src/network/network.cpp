// =============================================================================
// network.cpp — WiFi (portal cautivo + NVS) + MQTT a HiveMQ Cloud (TLS).
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
//   * Cliente PubSubClient sobre WiFiClientSecure (TLS) hacia HiveMQ Cloud:8883.
//   * Publica JSON de sensores en MQTT_TOPIC_SENSORS cada MQTT_PUBLISH_PERIOD_MS.
//   * Se suscribe a MQTT_TOPIC_CMD_PUMP (bomba) y MQTT_TOPIC_CMD_PLAY (melodía)
//     para recibir comandos remotos enviados por la Raspberry Pi.
// =============================================================================
#include "network.h"
#include "../core/config.h"
#include "../core/settings.h"
#include "../storage/sd_store.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>

// Root CA de HiveMQ Cloud. HiveMQ Cloud emite certificados con Let's Encrypt,
// cuya raíz es "ISRG Root X1". Validar contra esta CA evita ataques MITM. Si la
// conexión TLS fallara tras una rotación de CA, como último recurso se puede
// usar s_tls.setInsecure() (NO recomendado: desactiva la validación).
static const char HIVEMQ_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

static WiFiClientSecure s_tls;
static PubSubClient     s_mqtt(s_tls);
static Preferences      s_prefs;

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
// MQTT — HiveMQ Cloud (TLS) es el único broker.
// =============================================================================

// -----------------------------------------------------------------------------
// onMqttMessage — callback de los comandos publicados por la Raspberry Pi.
// El ESP se suscribe al comodín "matera/cmd/#" y despacha por topic exacto.
// Los payloads son texto plano (sin JSON) para que el parseo sea trivial.
// -----------------------------------------------------------------------------
static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  char buf[40] = {0};
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  logf("[mqtt] rx topic=%s payload=%s", topic, buf);

  if (strcmp(topic, MQTT_TOPIC_CMD_PUMP) == 0) {
    // "1" = regar ahora (ciclo manual); "0" = detener el ciclo en curso.
    IrrigationCmd cmd = (atoi(buf) != 0) ? IRR_CMD_MANUAL_START : IRR_CMD_STOP;
    xQueueSend(qIrrigationCmd, &cmd, 0);

  } else if (strcmp(topic, MQTT_TOPIC_CMD_PLAY) == 0) {
    // Reproducir la canción con el índice indicado.
    int idx = atoi(buf);
    if (idx >= 0 && idx < sd_songCount()) xQueueSend(qAudioCmd, &idx, 0);

  } else if (strcmp(topic, MQTT_TOPIC_CMD_STOP) == 0) {
    int stop = AUDIO_CMD_STOP;
    xQueueSend(qAudioCmd, &stop, 0);

  } else if (strcmp(topic, MQTT_TOPIC_CMD_VOLUME) == 0) {
    int level = atoi(buf);
    int cmd = AUDIO_VOL_CMD(level);
    xQueueSend(qAudioCmd, &cmd, 0);   // la tarea de audio refleja el nivel en settings

  } else if (strcmp(topic, MQTT_TOPIC_CMD_WSONG) == 0) {
    int idx = atoi(buf);
    if (idx >= 0 && idx < sd_songCount()) settings_setWateringSong(idx);

  } else if (strcmp(topic, MQTT_TOPIC_CMD_THRESHOLD) == 0) {
    settings_setThreshold((float)atof(buf));

  } else if (strcmp(topic, MQTT_TOPIC_CMD_NEO_BRIGHT) == 0) {
    int v = atoi(buf);
    if (v < 0) v = 0; if (v > 255) v = 255;
    settings_setBrightnessBoth((uint8_t)v);

  } else if (strcmp(topic, MQTT_TOPIC_CMD_NEO_PATTERN) == 0) {
    settings_setNeoPattern(buf);

  } else if (strcmp(topic, MQTT_TOPIC_CMD_NEO_COLOR) == 0) {
    // "RRGGBB" hexadecimal (acepta un '#' inicial opcional).
    const char* p = (buf[0] == '#') ? buf + 1 : buf;
    long rgb = strtol(p, nullptr, 16);
    settings_setNeoColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  }
}

// -----------------------------------------------------------------------------
// mqttReconnect — connect con credenciales MQTT de HiveMQ Cloud.
// -----------------------------------------------------------------------------
static bool mqttReconnect() {
  if (s_mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  bool ok = s_mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
  if (ok) {
    // Un único comodín cubre todos los comandos de la Raspi.
    s_mqtt.subscribe(MQTT_TOPIC_CMD_WILDCARD);
    xEventGroupSetBits(evtSystem, EVT_MQTT_CONNECTED);
    // Publicar el estado/catálogo apenas conectamos, para que la Raspi pueble
    // su dashboard (canciones, volumen, umbral, patrón, etc.).
    xEventGroupSetBits(evtSystem, EVT_STATE_DIRTY);
    logf("[mqtt] conectado a HiveMQ. sub: %s", MQTT_TOPIC_CMD_WILDCARD);
  } else {
    xEventGroupClearBits(evtSystem, EVT_MQTT_CONNECTED);
    logf("[mqtt] connect FAILED rc=%d", s_mqtt.state());
  }
  return ok;
}

// -----------------------------------------------------------------------------
// publishSensors — publica el snapshot de sensores como JSON en
// MQTT_TOPIC_SENSORS. La Raspberry Pi lo parsea para el dashboard.
// -----------------------------------------------------------------------------
static void publishSensors(const SensorData& s) {
  char payload[224];
  snprintf(payload, sizeof(payload),
    "{\"soil\":%.2f,\"temp\":%.2f,\"hum\":%.2f,"
     "\"lux\":%.1f,\"ppm\":%.0f,\"irrigating\":%s,\"ts\":%lu}",
    s.soilMoisturePct, s.tempC, s.humPct, s.lux, s.airQualityPpm,
    (xEventGroupGetBits(evtSystem) & EVT_IRRIGATING) ? "true" : "false",
    (unsigned long)s.timestampMs);

  s_mqtt.publish(MQTT_TOPIC_SENSORS, payload);
  logf("[mqtt] pub %s -> %s", MQTT_TOPIC_SENSORS, payload);
}

// -----------------------------------------------------------------------------
// jsonEscape — escapa comillas/barras de un nombre de archivo para JSON.
// -----------------------------------------------------------------------------
static void jsonEscapeInto(String& out, const char* s) {
  for (const char* p = s; *p; ++p) {
    if (*p == '"' || *p == '\\') out += '\\';
    out += *p;
  }
}

// -----------------------------------------------------------------------------
// publishState — publica el estado/catálogo como JSON RETENIDO en
// MQTT_TOPIC_STATE: lista de canciones de la SD, canción de riego, volumen,
// umbral, brillo/patrón/color de los NeoPixel y la lista de patrones soportados.
// Retenido para que la Raspi reciba el snapshot apenas se suscribe.
// -----------------------------------------------------------------------------
static void publishState() {
  RuntimeSettings st;
  settings_get(st);

  String out;
  out.reserve(900);
  out += "{\"songs\":[";
  int n = sd_songCount();
  for (int i = 0; i < n; ++i) {
    if (i) out += ',';
    out += '"';
    jsonEscapeInto(out, sd_songName(i));
    out += '"';
  }
  out += "],";
  out += "\"wateringSong\":" + String(st.wateringSongIndex) + ",";
  out += "\"volume\":" + String(st.audioVolume) + ",";
  out += "\"volumeMax\":" + String(AUDIO_VOLUME_MAX) + ",";
  out += "\"threshold\":" + String(st.soilThresholdPct, 1) + ",";
  out += "\"neoBright\":" + String(st.neoBrightness1) + ",";
  out += "\"neoBright2\":" + String(st.neoBrightness2) + ",";
  out += "\"neoPattern\":\"" + String(st.neoPattern) + "\",";
  char color[8];
  snprintf(color, sizeof(color), "%02X%02X%02X", st.neoR, st.neoG, st.neoB);
  out += "\"neoColor\":\"" + String(color) + "\",";
  out += "\"patterns\":[";
  // NEO_PATTERNS_CSV -> array JSON.
  const char* csv = NEO_PATTERNS_CSV;
  bool first = true;
  String tok;
  for (const char* p = csv; ; ++p) {
    if (*p == ',' || *p == '\0') {
      if (tok.length()) {
        if (!first) out += ',';
        out += '"'; out += tok; out += '"';
        first = false;
        tok = "";
      }
      if (*p == '\0') break;
    } else {
      tok += *p;
    }
  }
  out += "]}";

  s_mqtt.publish(MQTT_TOPIC_STATE, out.c_str(), /*retained=*/true);
  logf("[mqtt] pub %s (retained, %u bytes)", MQTT_TOPIC_STATE,
       (unsigned)out.length());
}

// =============================================================================
// taskMQTT — mantiene la conexión, publica cada MQTT_PUBLISH_PERIOD_MS y
// procesa subs entrantes. Período de loop 200 ms (necesario para PubSubClient).
// =============================================================================
void taskMQTT(void* arg) {
  s_tls.setCACert(HIVEMQ_ROOT_CA);   // validar el certificado de HiveMQ Cloud
  s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
  s_mqtt.setCallback(onMqttMessage);
  // Buffer amplio: el JSON de estado (catálogo de canciones) puede ser grande.
  s_mqtt.setBufferSize(2048);

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

      // Republicar el estado retenido cuando algún ajuste cambió (lo marca
      // cualquier setter de settings, o la reconexión MQTT).
      if (s_mqtt.connected() &&
          (xEventGroupClearBits(evtSystem, EVT_STATE_DIRTY) & EVT_STATE_DIRTY)) {
        publishState();
      }

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
