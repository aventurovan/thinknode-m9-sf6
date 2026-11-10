#include "M9Lcd.h"
#include <SPI.h>

static void lcdCmd(uint8_t c) {
  digitalWrite(M9LCD_PIN_DC, LOW);
  SPI.transfer(c);
  digitalWrite(M9LCD_PIN_DC, HIGH);
}

static void lcdRange(uint8_t cmd, uint16_t a, uint16_t b) {
  lcdCmd(cmd);
  SPI.transfer(a >> 8); SPI.transfer(a & 0xFF);
  SPI.transfer(b >> 8); SPI.transfer(b & 0xFF);
}

void m9lcdBlit(int x, int y, int w, int h, const uint16_t* pixels) {
  if (w <= 0 || h <= 0) return;
  SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  digitalWrite(M9LCD_PIN_CS, LOW);
  lcdRange(0x2A, x, x + w - 1);     // kolumny
  lcdRange(0x2B, y, y + h - 1);     // wiersze
  lcdCmd(0x2C);                     // zapis do pamięci ekranu
  SPI.writeBytes((const uint8_t*)pixels, (uint32_t)w * h * 2);
  digitalWrite(M9LCD_PIN_CS, HIGH);
  SPI.endTransaction();
}
