// =============================================================================
// audio.cpp — Reproductor de MP3 desde microSD por I2S (MAX98357A).
//
// Implementación calcada del código de prueba (vTareaAudio): un único hilo
// posee el AudioGeneratorMP3, el AudioFileSourceSD y el AudioOutputI2S, recibe
// comandos por qAudioCmd y bombea el decodificador con mp3->loop().
//
// Convención de comandos (int) — ver tasks.h:
//   * idx >= 0                 -> reproducir sd_songPath(idx)
//   * AUDIO_CMD_STOP   (-1)    -> detener la reproducción actual
//   * AUDIO_VOL_CMD(n) (-2..-6)-> fijar volumen al nivel n (0..AUDIO_VOLUME_MAX)
// =============================================================================
#include "audio.h"
#include "../core/config.h"
#include "../core/settings.h"
#include "../storage/sd_store.h"

#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

// Tabla de ganancias por nivel (idéntica al test). 0 = mudo, 4 = máximo.
static const float kGanancias[AUDIO_VOLUME_MAX + 1] = { 0.00f, 0.08f, 0.22f, 0.55f, 1.00f };

static int s_volumeLevel = AUDIO_VOLUME_DEFAULT;

// =============================================================================
// audio_init — sin trabajo pesado aquí: la salida I2S se construye dentro de la
// tarea (igual que en el test) para que todos los objetos de audio vivan en el
// mismo contexto de ejecución. Sólo validamos el rango del volumen por defecto.
// =============================================================================
bool audio_init() {
  if (s_volumeLevel < 0) s_volumeLevel = 0;
  if (s_volumeLevel > AUDIO_VOLUME_MAX) s_volumeLevel = AUDIO_VOLUME_MAX;
  return true;
}

// =============================================================================
// taskAudio — posee los objetos de ESP8266Audio y atiende la cola de comandos.
// =============================================================================
void taskAudio(void* arg) {
  AudioGeneratorMP3* mp3        = new AudioGeneratorMP3();
  AudioFileSourceSD* fileSource = nullptr;
  AudioOutputI2S*    out        = new AudioOutputI2S();
  out->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  out->SetGain(kGanancias[s_volumeLevel]);

  int  cmd          = AUDIO_CMD_STOP;
  bool reproduciendo = false;

  for (;;) {
    // ---- Procesar todos los comandos pendientes (no bloqueante) ----
    while (xQueueReceive(qAudioCmd, &cmd, 0) == pdPASS) {
      if (cmd <= AUDIO_VOL_CMD(0) && cmd >= AUDIO_VOL_CMD(AUDIO_VOLUME_MAX)) {
        // Comando de volumen: nivel = -(cmd) - 2
        int nivel = (-cmd) - 2;
        if (nivel < 0) nivel = 0;
        if (nivel > AUDIO_VOLUME_MAX) nivel = AUDIO_VOLUME_MAX;
        s_volumeLevel = nivel;
        out->SetGain(kGanancias[nivel]);
        // Reflejar el nivel en settings para que el estado MQTT (y ambos
        // dashboards) muestren el volumen vigente.
        settings_setVolume(nivel);
        logf("[audio] volumen=%d", nivel);
      } else if (cmd == AUDIO_CMD_STOP) {
        if (reproduciendo) {
          mp3->stop();
          if (fileSource) { fileSource->close(); delete fileSource; fileSource = nullptr; }
          reproduciendo = false;
          xEventGroupClearBits(evtSystem, EVT_AUDIO_PLAYING);
          logf("[audio] stop");
        }
      } else if (cmd >= 0 && cmd < sd_songCount()) {
        // Cambiar de canción: cortar la actual y abrir la nueva.
        if (reproduciendo) {
          mp3->stop();
          if (fileSource) { fileSource->close(); delete fileSource; fileSource = nullptr; }
          reproduciendo = false;
        }
        String ruta = sd_songPath(cmd);
        fileSource = new AudioFileSourceSD(ruta.c_str());
        if (fileSource->isOpen() && mp3->begin(fileSource, out)) {
          reproduciendo = true;
          xEventGroupSetBits(evtSystem, EVT_AUDIO_PLAYING);
          logf("[audio] play [%d] %s", cmd, ruta.c_str());
        } else {
          delete fileSource; fileSource = nullptr;
          logf("[audio] ERROR abriendo %s", ruta.c_str());
        }
      }
    }

    // ---- Bombear el decodificador ----
    if (reproduciendo) {
      if (mp3->isRunning()) {
        if (!mp3->loop()) {                 // fin del archivo
          mp3->stop();
          if (fileSource) { fileSource->close(); delete fileSource; fileSource = nullptr; }
          reproduciendo = false;
          xEventGroupClearBits(evtSystem, EVT_AUDIO_PLAYING);
        }
      } else {
        reproduciendo = false;
        xEventGroupClearBits(evtSystem, EVT_AUDIO_PLAYING);
      }
    }

    // Mientras reproduce conviene ceder poco la CPU para no cortar el audio.
    vTaskDelay(reproduciendo ? pdMS_TO_TICKS(2) : pdMS_TO_TICKS(40));
  }
}
