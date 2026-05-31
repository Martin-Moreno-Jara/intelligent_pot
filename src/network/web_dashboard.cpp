// =============================================================================
// web_dashboard.cpp — Dashboard local servido por el propio ESP32.
//
// Además de mostrar las lecturas (polling a /api/sensors), expone controles:
//   * Botón "Regar ahora"            -> POST /api/water
//   * Selector de canción de la SD   -> /api/songs (lista) + POST /api/play
//   * Umbral de humedad              -> POST /api/threshold
//   * Intervalo de riego             -> POST /api/irr_interval
//   * Brillo independiente NeoPixel  -> POST /api/bright?r=1|2&v=0..255
// =============================================================================
#include "web_dashboard.h"
#include "../core/config.h"
#include "../core/settings.h"
#include "../storage/sd_store.h"
#include <WiFi.h>
#include <WebServer.h>

// HTML servido en /. Es estático salvo el placeholder del intervalo de refresco.
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
  h2 { color: #2a9; font-size: 18px; margin: 24px 0 8px; }
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
  .panel { background:#fff; border-radius:8px; padding:16px; max-width:720px;
           box-shadow:0 1px 3px rgba(0,0,0,0.08); margin-bottom:12px; }
  .row { display:flex; align-items:center; gap:10px; flex-wrap:wrap;
         margin:8px 0; }
  .row > label { min-width:150px; color:#555; font-size:14px; }
  button { padding:10px 16px; background:#2a9; color:#fff; border:0;
           border-radius:6px; font-size:14px; cursor:pointer; }
  button.warn { background:#e67e22; }
  button:active { opacity:.8; }
  input[type=number], select { padding:8px; font-size:14px; border:1px solid #ccc;
           border-radius:6px; }
  input[type=range] { flex:1; min-width:140px; }
  .hint { color:#999; font-size:12px; }
  #status { margin-left:8px; font-size:13px; color:#2a9; }
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
    <div class="card"><div class="label">Luz</div>
      <div class="value"><span id="lux">--</span><span class="unit">lx</span></div></div>
    <div class="card"><div class="label">Calidad de aire</div>
      <div class="value"><span id="ppm">--</span><span class="unit">ppm</span></div></div>
  </div>
  <div class="meta">
    &Uacute;ltima actualizaci&oacute;n: <span id="ts">--</span>
    &nbsp;|&nbsp; Riego: <span id="irr">--</span>
  </div>

  <h2>Controles</h2>

  <div class="panel">
    <div class="row">
      <label>Riego manual</label>
      <button class="warn" onclick="post('/api/water','Riego iniciado')">Regar ahora</button>
      <span class="hint">Pulso de 5 s inmediato, sin afectar el temporizador.</span>
    </div>
    <div class="row">
      <label>Umbral de humedad</label>
      <input type="number" id="thrLow" min="0" max="100" step="1" style="width:90px">
      <span class="unit">%</span>
      <button onclick="setThresholdLow()">Aplicar</button>
      <span class="hint">Solo riega si la humedad est&aacute; por debajo al vencer el intervalo.</span>
    </div>
    <div class="row">
      <label>Intervalo de riego</label>
      <input type="number" id="irrInterval" min="0.1" max="720" step="0.5" style="width:90px">
      <span class="unit">h</span>
      <button onclick="setIrrInterval()">Aplicar</button>
      <span class="hint">Tiempo entre revisiones autom&aacute;ticas.</span>
    </div>
  </div>

  <div class="panel">
    <div class="row">
      <label>Brillo anillo 1</label>
      <input type="range" id="b1" min="0" max="255"
             oninput="b1v.textContent=this.value" onchange="setBright(1,this.value)">
      <span id="b1v">--</span>
    </div>
    <div class="row">
      <label>Brillo anillo 2</label>
      <input type="range" id="b2" min="0" max="255"
             oninput="b2v.textContent=this.value" onchange="setBright(2,this.value)">
      <span id="b2v">--</span>
    </div>
  </div>

  <span id="status"></span>

<script>
const REFRESH_MS = __REFRESH_MS__;
const fmt = (v, d) => (v === null || v === undefined || isNaN(v)) ? '--' : Number(v).toFixed(d);

function flash(msg) {
  const s = document.getElementById('status');
  s.textContent = msg;
  setTimeout(() => { s.textContent = ''; }, 2500);
}

async function post(url, okmsg) {
  try {
    const r = await fetch(url, { method: 'POST' });
    if (!r.ok) throw new Error('http ' + r.status);
    flash(okmsg || 'OK');
  } catch (e) { flash('Error: ' + e.message); }
}

function playNow() {
  const i = document.getElementById('songsel').value;
  if (i === '') return flash('No hay canciones en la SD');
  post('/api/play?i=' + i, 'Reproduciendo');
}
function setThresholdLow() {
  const v = document.getElementById('thrLow').value;
  post('/api/threshold?v=' + encodeURIComponent(v), 'Umbral = ' + v + '%');
}
function setIrrInterval() {
  const v = document.getElementById('irrInterval').value;
  post('/api/irr_interval?v=' + encodeURIComponent(v), 'Intervalo = ' + v + ' h');
}
function setBright(ring, v) {
  post('/api/bright?r=' + ring + '&v=' + v, 'Brillo anillo ' + ring + ' = ' + v);
}

async function loadState() {
  try {
    const r = await fetch('/api/songs', { cache: 'no-store' });
    const d = await r.json();
    const sel = document.getElementById('songsel');
    sel.innerHTML = '';
    (d.songs || []).forEach((name, i) => {
      const o = document.createElement('option');
      o.value = i; o.textContent = name;
      sel.appendChild(o);
    });
    document.getElementById('thrLow').value     = d.thresholdLow;
    document.getElementById('irrInterval').value = d.irrigationInterval;
    document.getElementById('b1').value = d.b1; document.getElementById('b1v').textContent = d.b1;
    document.getElementById('b2').value = d.b2; document.getElementById('b2v').textContent = d.b2;
  } catch (e) { flash('No se pudo leer estado'); }
}

async function tick() {
  try {
    const r = await fetch('/api/sensors', { cache: 'no-store' });
    if (!r.ok) throw new Error('http ' + r.status);
    const d = await r.json();
    document.getElementById('soil').textContent = fmt(d.soil, 1);
    document.getElementById('temp').textContent = fmt(d.temp, 1);
    document.getElementById('hum').textContent  = fmt(d.hum,  1);
    document.getElementById('lux').textContent  = fmt(d.lux,  0);
    document.getElementById('ppm').textContent  = fmt(d.ppm,  0);
    document.getElementById('ts').textContent   = new Date().toLocaleTimeString();
    document.getElementById('irr').textContent  = d.irrigating ? 'activo' : 'inactivo';
    document.body.classList.remove('stale');
  } catch (e) {
    document.body.classList.add('stale');
  }
}
loadState();
tick();
setInterval(tick, REFRESH_MS);
</script>
</body>
</html>
)HTML";

// -----------------------------------------------------------------------------
// renderIndex — sustituye el placeholder del intervalo de refresco.
// -----------------------------------------------------------------------------
static String renderIndex() {
  String page = FPSTR(DASHBOARD_HTML);
  page.replace("__REFRESH_MS__", String(WEB_DASHBOARD_REFRESH_MS));
  return page;
}

// -----------------------------------------------------------------------------
// jsonEscape — escapa comillas y barras de un nombre de archivo para JSON.
// -----------------------------------------------------------------------------
static String jsonEscape(const char* s) {
  String out;
  for (const char* p = s; *p; ++p) {
    if (*p == '"' || *p == '\\') out += '\\';
    out += *p;
  }
  return out;
}

// -----------------------------------------------------------------------------
// renderSensorsJson — snapshot a JSON (no destructivo).
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
     "\"lux\":%.1f,\"ppm\":%.0f,"
     "\"irrigating\":%s,\"ts\":%lu}",
    s.soilMoisturePct, s.tempC, s.humPct,
    s.lux, s.airQualityPpm,
    irrigating ? "true" : "false",
    (unsigned long)s.timestampMs);
  return String(buf);
}

// -----------------------------------------------------------------------------
// renderStateJson — catálogo de canciones de la SD + valores de settings, para
// que la UI pueble el selector y refleje umbral/brillo/canción de riego.
// -----------------------------------------------------------------------------
static String renderStateJson() {
  RuntimeSettings st;
  settings_get(st);

  String out;
  out.reserve(512);
  out += "{\"songs\":[";
  int n = sd_songCount();
  for (int i = 0; i < n; ++i) {
    if (i) out += ',';
    out += '"';
    out += jsonEscape(sd_songName(i));
    out += '"';
  }
  out += "],";
  out += "\"thresholdLow\":" + String(st.soilThresholdLowPct, 1) + ",";
  out += "\"irrigationInterval\":" + String(st.irrigationIntervalHrs, 1) + ",";
  out += "\"b1\":" + String(st.neoBrightness1) + ",";
  out += "\"b2\":" + String(st.neoBrightness2);
  out += "}";
  return out;
}

// =============================================================================
// taskDashboard — espera a la STA, arranca el WebServer y atiende rutas.
// =============================================================================
void taskDashboard(void* arg) {
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

  server.on("/api/songs", HTTP_GET, [&]() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", renderStateJson());
  });

  // ---- Controles (POST) ----
  server.on("/api/water", HTTP_POST, [&]() {
    IrrigationCmd cmd = IRR_CMD_MANUAL_START;
    xQueueSend(qIrrigationCmd, &cmd, 0);
    logf("[web] riego manual solicitado");
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/play", HTTP_POST, [&]() {
    int idx = server.arg("i").toInt();
    if (idx >= 0 && idx < sd_songCount()) {
      xQueueSend(qAudioCmd, &idx, 0);
      logf("[web] reproducir canción %d", idx);
      server.send(200, "text/plain", "ok");
    } else {
      server.send(400, "text/plain", "indice invalido");
    }
  });

  server.on("/api/threshold", HTTP_POST, [&]() {
    if (!server.hasArg("v")) { server.send(400, "text/plain", "falta v"); return; }
    float v = server.arg("v").toFloat();
    settings_setThresholdLow(v);
    logf("[web] umbral humedad = %.1f%%", v);
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/irr_interval", HTTP_POST, [&]() {
    if (!server.hasArg("v")) { server.send(400, "text/plain", "falta v"); return; }
    float v = server.arg("v").toFloat();
    settings_setIrrigationInterval(v);
    logf("[web] intervalo riego = %.1f h", v);
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/bright", HTTP_POST, [&]() {
    if (!server.hasArg("r") || !server.hasArg("v")) {
      server.send(400, "text/plain", "faltan args"); return;
    }
    int ring = server.arg("r").toInt();
    int val  = server.arg("v").toInt();
    if (val < 0) val = 0; if (val > 255) val = 255;
    if (ring == 1)      settings_setBrightness1((uint8_t)val);
    else if (ring == 2) settings_setBrightness2((uint8_t)val);
    else { server.send(400, "text/plain", "anillo invalido"); return; }
    logf("[web] brillo anillo %d = %d", ring, val);
    server.send(200, "text/plain", "ok");
  });

  server.onNotFound([&]() {
    server.send(404, "text/plain", "not found");
  });

  server.begin();
  logf("[web] dashboard escuchando en http://%s:%u/",
       WiFi.localIP().toString().c_str(),
       (unsigned)WEB_DASHBOARD_PORT);

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      server.handleClient();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
