#pragma once
// =============================================================================
// sd_store.h — Tarjeta microSD (HW-203) en bus HSPI dedicado + índice de MP3.
//
// Replica la inicialización del código de prueba: la SD vive en su propio bus
// SPI (spiSD = HSPI) para no chocar con el TFT (que usa el SPI por defecto).
// El driver de audio (ESP8266Audio / AudioFileSourceSD) lee los archivos del
// objeto global SD que aquí se monta.
//
// El catálogo de canciones se construye una sola vez en sd_init(); a partir de
// ahí es de sólo lectura y lo consultan la tarea de audio y el dashboard.
// =============================================================================

#include <Arduino.h>

// Monta la SD en HSPI y escanea la raíz buscando archivos .mp3. Devuelve true
// si la tarjeta se montó (aunque no haya canciones).
bool sd_init();

// Número de MP3 indexados.
int sd_songCount();

// Nombre de archivo del MP3 i (sin barra inicial), p.ej. "lluvia.mp3".
// Devuelve "" si el índice es inválido.
const char* sd_songName(int i);

// Ruta absoluta para abrir el MP3 i con AudioFileSourceSD, p.ej. "/lluvia.mp3".
// Devuelve "" si el índice es inválido.
String sd_songPath(int i);
