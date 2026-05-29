#pragma once
// =============================================================================
// network.h — Conectividad:
//   * WiFi: aprovisionamiento por portal cautivo (AP + DNS + WebServer) la
//     primera vez; las credenciales se persisten en NVS (Preferences). En
//     boots posteriores se intentan las credenciales guardadas; si fallan se
//     vuelve a abrir el portal. Modo de alto rendimiento (TX max, sin
//     power-save) durante scan/autenticación para entornos con muchas redes.
//   * MQTT: HiveMQ Cloud (TLS) es el ÚNICO broker. Publica los sensores en
//     formato JSON en MQTT_TOPIC_SENSORS y se suscribe a MQTT_TOPIC_CMD_PUMP y
//     MQTT_TOPIC_CMD_PLAY para los comandos remotos que envía la Raspberry Pi.
//     El dashboard web lo sirve exclusivamente la Raspberry Pi.
// =============================================================================

#include "../core/tasks.h"

// Bloqueante: si hay credenciales en NVS las usa; si no, lanza el portal AP y
// no retorna hasta que el usuario provee unas que funcionan. Al volver, la
// STA está conectada y el AP apagado.
bool network_initWiFi();

void taskMQTT(void* arg);
