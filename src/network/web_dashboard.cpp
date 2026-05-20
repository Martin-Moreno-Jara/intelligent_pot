// =============================================================================
// web_dashboard.cpp — Dashboard local servido por el propio ESP32.
//
// El cliente carga / una vez y luego hace polling a /api/sensors cada
// WEB_DASHBOARD_REFRESH_MS para refrescar los valores. Coexiste con la
// conexión STA al router doméstico: misma interfaz, distinto puerto al portal
// cautivo (que sólo vive durante el aprovisionamiento).
// =============================================================================
#include "web_dashboard.h"
#include "../core/config.h"
#include <WiFi.h>
#include <WebServer.h>

// HTML servido en /. Es estático: una sola lectura del firmware. El refresco
// de valores lo hace el JS embebido haciendo fetch() a /api/sensors.
static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Matera inteligente</title>
<style>
  body { font-family: sans-serif; margin: 0; padding: 20px;
         background: #f4f7f6; color: #222; }
  h1 { color: #2a9; margin-top: 0; }
  .grid { display: grid; gap: 12px;
          grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
          max-width: 720px; }
  .card { background: #fff; border-radius: 8px; padding: 16px;
          box-shadow: 0 1px 3px rgba(0,0,0,0.08); }
  .label { font-size: 12px; color: #777; text-transform: uppercase;
           letter-spacing: 0.05em; }
  .value { font-size: 28px; font-weight: 600; margin-top: 6px; }
  .unit  { font-size: 14px; color: #555; margin-left: 4px; }
  .meta  { margin-top: 16px; color: #888; font-size: 12px; max-width: 720px; }
  .stale .value { color: #c33; }
</style>
</head>
<body>
  <h1>Matera inteligente</h1>
  <div class="grid" id="grid">
    <div class="card"><div class="label">Humedad del suelo</div>
      <div class="value"><span id="soil">--</span><span class="unit">%</span></div></div>
    <div class="card"><div class="label">Temperatura</div>
      <div class="value"><span id="temp">--</span><span class="unit">&deg;C</span></div></div>
    <div class="card"><div class="label">Humedad ambiente</div>
      <div class="value"><span id="hum">--</span><span class="unit">%</span></div></div>
    <div class="card"><div class="label">Presi&oacute;n</div>
      <div class="value"><span id="pres">--</span><span class="unit">hPa</span></div></div>
    <div class="card"><div class="label">Luz</div>
      <div class="value"><span id="lux">--</span><span class="unit">lx</span></div></div>
    <div class="card"><div class="label">Calidad de aire</div>
      <div class="value"><span id="ppm">--</span><span class="unit">ppm</span></div></div>
  </div>
  <div class="meta">
    &Uacute;ltima actualizaci&oacute;n: <span id="ts">--</span>
    &nbsp;|&nbsp; Riego: <span id="irr">--</span>
  </div>
<script>
const REFRESH_MS = __REFRESH_MS__;
const fmt = (v, d) => (v === null || v === undefined || isNaN(v)) ? '--' : Number(v).toFixed(d);
async function tick() {
  try {
    const r = await fetch('/api/sensors', { cache: 'no-store' });
    if (!r.ok) throw new Error('http ' + r.status);
    const d = await r.json();
    document.getElementById('soil').textContent = fmt(d.soil, 1);
    document.getElementById('temp').textContent = fmt(d.temp, 1);
    document.getElementById('hum').textContent  = fmt(d.hum,  1);
    document.getElementById('pres').textContent = fmt(d.pres, 1);
    document.getElementById('lux').textContent  = fmt(d.lux,  0);
    document.getElementById('ppm').textContent  = fmt(d.ppm,  0);
    document.getElementById('ts').textContent   = new Date().toLocaleTimeString();
    document.getElementById('irr').textContent  = d.irrigating ? 'activo' : 'inactivo';
    document.body.classList.remove('stale');
  } catch (e) {
    document.body.classList.add('stale');
  }
}
tick();
setInterval(tick, REFRESH_MS);
</script>
</body>
</html>
)HTML";

// -----------------------------------------------------------------------------
// renderIndex — sustituye el placeholder del intervalo de refresco. El resto
// del HTML es estático, así que esta función es la única que produce churn por
// request a /.
// -----------------------------------------------------------------------------
static String renderIndex() {
  String page = FPSTR(DASHBOARD_HTML);
  page.replace("__REFRESH_MS__", String(WEB_DASHBOARD_REFRESH_MS));
  return page;
}

// -----------------------------------------------------------------------------
// renderSensorsJson — snapshot a JSON. Usa xQueuePeek (no destructivo) para no
// afectar a otros consumidores de qSensorData.
// -----------------------------------------------------------------------------
static String renderSensorsJson() {
  SensorData s{};
  bool have = (xQueuePeek(qSensorData, &s, 0) == pdTRUE);
  bool irrigating = (xEventGroupGetBits(evtSystem) & EVT_IRRIGATING) != 0;

  if (!have) {
    return String("{\"ready\":false}");
  }
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"ready\":true,"
     "\"soil\":%.2f,\"temp\":%.2f,\"hum\":%.2f,"
     "\"pres\":%.2f,\"lux\":%.1f,\"ppm\":%.0f,"
     "\"irrigating\":%s,\"ts\":%lu}",
    s.soilMoisturePct, s.tempC, s.humPct,
    s.pressureHPa, s.lux, s.airQualityPpm,
    irrigating ? "true" : "false",
    (unsigned long)s.timestampMs);
  return String(buf);
}

// =============================================================================
// taskDashboard — espera a que la STA esté asociada (evtSystem) y arranca el
// WebServer en la IP local. handleClient() es bloqueante corto: 50 ms de loop
// es suficiente para servir un dashboard que sólo refresca cada ~2 s.
// =============================================================================
void taskDashboard(void* arg) {
  // Esperar al primer EVT_WIFI_CONNECTED antes de bind: si la STA no tiene IP
  // todavía, server.begin() arranca igual, pero no queremos loguear una IP
  // 0.0.0.0 y confundir al usuario.
  xEventGroupWaitBits(evtSystem, EVT_WIFI_CONNECTED,
                      pdFALSE, pdTRUE, portMAX_DELAY);

  WebServer server(WEB_DASHBOARD_PORT);

  server.on("/", HTTP_GET, [&]() {
    server.send(200, "text/html; charset=utf-8", renderIndex());
  });

  server.on("/api/sensors", HTTP_GET, [&]() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", renderSensorsJson());
  });

  server.onNotFound([&]() {
    server.send(404, "text/plain", "not found");
  });

  server.begin();
  logf("[web] dashboard escuchando en http://%s:%u/",
       WiFi.localIP().toString().c_str(),
       (unsigned)WEB_DASHBOARD_PORT);

  for (;;) {
    // Si perdimos WiFi, el WebServer no responde de todos modos; sólo
    // esperamos a que vuelva la asociación y seguimos sirviendo.
    if (WiFi.status() == WL_CONNECTED) {
      server.handleClient();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
