#pragma once
// =============================================================================
// network.h — WiFi + MQTT (pub/sub) + ThingSpeak (HTTP).
// Dos tareas (MQTT y ThingSpeak) corren en core 0 junto al stack WiFi.
// =============================================================================

#include "../core/tasks.h"

bool network_initWiFi();    // bloqueante, espera WIFI_CONNECT_TIMEOUT_MS
void taskMQTT(void* arg);
void taskThingSpeak(void* arg);
