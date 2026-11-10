#pragma once
// Sterownik klawiatury ThinkNode M9 (osobny procesor STC8H na I2C) — własna implementacja (MIT).
// Wersja testowa: szuka klawiatury na obu magistralach, czyta surowe rejestry, liczy przerwania.

#include <Arduino.h>
#include <Wire.h>

#define M9KB_ADDR_V1   0x6C
#define M9KB_ADDR_V2   0x6D
#define M9KB_PIN_INT   12     // w spoczynku LOW, zbocze narastające przy naciśnięciu
#define M9KB_PIN_LED   46     // podświetlenie klawiatury
#define M9KB_BUS1_SDA  20     // druga magistrala (Wire1)
#define M9KB_BUS1_SCL  21
#define M9KB_NUM_REGS  8      // rejestry 0x00..0x07 pokazywane na ekranie testu

class M9Keyboard {
  TwoWire* _wire = NULL;
  uint8_t  _addr = 0;
  uint8_t  _bus = 0xFF;        // 0 = Wire (SDA 7 / SCL 6), 1 = Wire1 (SDA 20 / SCL 21)

public:
  // lista adresów znalezionych przy skanie (dla każdej magistrali osobno)
  uint8_t found[2][8];
  uint8_t num_found[2] = { 0, 0 };

  void begin();
  void scan();                 // pełny skan obu magistral + wybór klawiatury
  bool isFound() const { return _wire != NULL; }
  uint8_t addr() const { return _addr; }
  uint8_t bus() const { return _bus; }

  int  readReg(uint8_t reg);   // -1 = błąd odczytu
  void readRegs(int16_t* dest, uint8_t count);   // rejestry 0..count-1, każdy osobno

  bool intLevel() const { return digitalRead(M9KB_PIN_INT) == HIGH; }
  uint32_t irqCount() const;
  bool takeIrq();              // true, jeśli od ostatniego wywołania było przerwanie

  void setBacklight(bool on) { digitalWrite(M9KB_PIN_LED, on ? HIGH : LOW); }
};
