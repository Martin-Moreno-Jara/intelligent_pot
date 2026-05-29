#pragma once
// =============================================================================
// audio.h — Reproducción de MP3 desde la microSD por I2S (MAX98357A).
//
// Usa la librería ESP8266Audio (AudioGeneratorMP3 + AudioFileSourceSD +
// AudioOutputI2S), EXACTAMENTE como el código de prueba. La tarea recibe
// comandos enteros por qAudioCmd (ver tasks.h para la convención):
//   * >= 0            -> reproducir la canción con ese índice (sd_store)
//   * AUDIO_CMD_STOP  -> detener
//   * volumen         -> AUDIO_VOL_CMD(nivel) con nivel 0..AUDIO_VOLUME_MAX
//
// La SD debe haberse montado antes (sd_init()).
// =============================================================================

#include "../core/tasks.h"

bool audio_init();           // prepara la salida I2S; la SD se monta aparte
void taskAudio(void* arg);   // bloquea esperando comandos y reproduce MP3
