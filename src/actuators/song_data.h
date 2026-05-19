#pragma once
// =============================================================================
// song_data.h — Datos PCM en PROGMEM para las canciones del sistema.
// Para mantener el firmware liviano se incluye un patrón de muestras corto
// (un período senoidal a 440 Hz @ 22.05 kHz) que el reproductor concatena
// según el patrón melódico. Reemplazar por WAV PCM 16-bit mono real cuando
// se necesite música pregrabada.
// =============================================================================

#include <Arduino.h>
#include <pgmspace.h>

// 50 muestras ~ 440 Hz @ 22050 Hz; el reproductor las cicla para "sostener" la nota.
static const int16_t SINE_440_22K[50] PROGMEM = {
       0,   4106,   8095,  11852,  15269,  18253,  20720,  22606,
   23861,  24452,  24365,  23601,  22182,  20141,  17532,  14421,
   10887,   7019,   2918,  -1310,  -5556, -9710, -13662, -17307,
  -20546, -23290, -25459, -26986, -27815, -27911, -27256, -25852,
  -23718, -20893, -17430, -13397, -8881,  -3978,    1139,   6296,
   11314,  16018,  20245,  23842,  26680,  28653,  29683,  29721,
   28751,  26789
};
static const size_t SINE_440_22K_LEN = sizeof(SINE_440_22K) / sizeof(int16_t);

// Melodía "agua": cada entrada (freqHz, ms). freqHz=0 = silencio.
struct Note { uint16_t freqHz; uint16_t ms; };

static const Note SONG_WATER[] PROGMEM = {
  {523, 150}, {659, 150}, {784, 200}, {0, 80},
  {784, 150}, {880, 250}, {0, 100},
  {659, 200}, {523, 300}
};
static const size_t SONG_WATER_LEN = sizeof(SONG_WATER) / sizeof(Note);

static const Note SONG_ALERT[] PROGMEM = {
  {880, 120}, {0, 60}, {880, 120}, {0, 60}, {880, 180}
};
static const size_t SONG_ALERT_LEN = sizeof(SONG_ALERT) / sizeof(Note);
