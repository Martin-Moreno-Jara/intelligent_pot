#pragma once
// =============================================================================
// network.h — Conectividad:
//   * WiFi: aprovisionamiento por portal cautivo (AP + DNS + WebServer) la
//     primera vez; las credenciales se persisten en NVS (Preferences). En
//     boots posteriores se intentan las credenciales guardadas; si fallan se
//     vuelve a abrir el portal. Modo de alto rendimiento (TX max, sin
//     power-save) durante scan/autenticación para entornos con muchas redes.
//   * MQTT: ThingSpeak es el ÚNICO broker. Publica sensores en
//     channels/<id>/publish y se suscribe a channels/<id>/subscribe/fields/N
//     para los comandos remotos.
// =============================================================================

#include "../core/tasks.h"

// Bloqueante: si hay credenciales en NVS las usa; si no, lanza el portal AP y
// no retorna hasta que el usuario provee unas que funcionan. Al volver, la
// STA está conectada y el AP apagado.
bool network_initWiFi();

void taskMQTT(void* arg);
