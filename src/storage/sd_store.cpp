// =============================================================================
// sd_store.cpp — Montaje de la microSD en HSPI + escaneo de MP3.
// Calca el flujo de listarCancionesSD() del código de prueba.
// =============================================================================
#include "sd_store.h"
#include "../core/config.h"
#include "../core/tasks.h"   // logf()
#include <SPI.h>
#include <FS.h>
#include <SD.h>

// Bus SPI dedicado para la SD (HSPI), independiente del SPI del display.
static SPIClass spiSD(HSPI);

static String s_songs[SD_MAX_SONGS];
static int    s_count = 0;

// =============================================================================
// sd_init — inicializa el bus HSPI, monta la SD y escanea los .mp3 de la raíz.
// =============================================================================
bool sd_init() {
  s_count = 0;

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  vTaskDelay(pdMS_TO_TICKS(15));

  spiSD.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, spiSD, SD_SPI_FREQ_HZ)) {
    logf("[sd] ERROR: no se pudo montar la tarjeta microSD");
    return false;
  }

  File root = SD.open("/");
  if (!root) {
    logf("[sd] ERROR: no se pudo abrir la raíz");
    return false;
  }

  File file = root.openNextFile();
  while (file && s_count < SD_MAX_SONGS) {
    if (!file.isDirectory()) {
      String name = String(file.name());
      if (name.length() > 4 &&
          name.substring(name.length() - 4).equalsIgnoreCase(".mp3")) {
        s_songs[s_count] = name;
        logf("[sd]  [%d] %s", s_count + 1, name.c_str());
        s_count++;
      }
    }
    file = root.openNextFile();
  }
  root.close();

  logf("[sd] escaneo completo. MP3 indexados: %d", s_count);
  return true;
}

int sd_songCount() { return s_count; }

const char* sd_songName(int i) {
  if (i < 0 || i >= s_count) return "";
  return s_songs[i].c_str();
}

String sd_songPath(int i) {
  if (i < 0 || i >= s_count) return String("");
  // El nombre puede o no venir con "/" según la versión del core; normalizamos.
  String n = s_songs[i];
  if (n.startsWith("/")) return n;
  return "/" + n;
}
