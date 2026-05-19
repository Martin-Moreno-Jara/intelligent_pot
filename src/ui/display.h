#pragma once
// =============================================================================
// display.h — OLED SSD1306 128x64, rotación automática de páginas.
// Comparte el bus I2C con los sensores; protegido con mtxI2C.
// =============================================================================

#include "../core/tasks.h"

bool display_init();
void taskDisplay(void* arg);
