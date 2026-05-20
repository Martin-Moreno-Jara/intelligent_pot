#pragma once
// =============================================================================
// display.h — TFT a color 1.3" (ST7789, 240x240) por SPI dedicado.
// Ya NO comparte bus con los sensores: no se toma mtxI2C.
// =============================================================================

#include "../core/tasks.h"

bool display_init();
void taskDisplay(void* arg);
