#pragma once
// Kolorowe rysowanie na ekranie ST7789 ThinkNode M9 — wersja testowa.
// Sterownik ekranu z MeshCore jest jednokolorowy, więc kolorowy obraz (mapę) wysyłamy do panelu sami,
// tą samą magistralą SPI. Współrzędne jak na ekranie: 320x240, (0,0) w lewym górnym rogu.

#include <Arduino.h>

#define M9LCD_PIN_CS  16
#define M9LCD_PIN_DC  15

// kolor RGB565 w kolejności bajtów panelu (starszy bajt pierwszy)
#define M9LCD_RGB(r, g, b)  ((uint16_t)(((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) >> 8) | (uint16_t)(((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) << 8))

// wysyła prostokąt pikseli (już w kolejności bajtów panelu)
void m9lcdBlit(int x, int y, int w, int h, const uint16_t* pixels);
