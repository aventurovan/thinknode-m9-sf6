#pragma once
#include <stdint.h>
void m9lcdBlit(int x, int y, int w, int h, const uint16_t* px);
inline void m9lcdBacklight(int) { }
