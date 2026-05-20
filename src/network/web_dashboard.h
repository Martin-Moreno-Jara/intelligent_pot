#pragma once
// =============================================================================
// web_dashboard.h — Servidor HTTP local que sirve el dashboard.
//   * GET /            -> página HTML con auto-refresh por JS
//   * GET /api/sensors -> JSON con el último SensorData de qSensorData
//
// El servidor escucha en la IP que el router le asignó al ESP (modo STA), en
// el puerto WEB_DASHBOARD_PORT. La IP se imprime por Serial y por logf() al
// completarse la asociación WiFi.
// =============================================================================

#include "../core/tasks.h"

void taskDashboard(void* arg);
