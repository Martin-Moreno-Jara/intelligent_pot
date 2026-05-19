#pragma once
// =============================================================================
// audio.h — Reproducción I2S vía MAX98357 (mono).
// La tarea bloquea esperando comandos en qAudioCmd y reproduce la melodía
// correspondiente sintetizando ondas senoidales en RAM a partir del patrón
// PROGMEM en song_data.h.
// =============================================================================

#include "../core/tasks.h"

bool audio_init();
void taskAudio(void* arg);
