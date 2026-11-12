#pragma once
// Sterownik klawiatury ThinkNode M9 (osobny procesor STC8H na I2C) — własna implementacja (MIT).
//
// Ważne: piny 20/21 tej magistrali to też piny wbudowanego USB procesora. M9 ma osobny układ USB-UART,
// więc wbudowane USB odłączamy od pinów — inaczej transmisja z klawiaturą jest przekłamana
// (działał tylko Enter). Ten fakt i numery rejestrów ustalono z publicznych projektów dla M9.

#include <Arduino.h>
#include <Wire.h>

#define M9KB_ADDR_V1   0x6C
#define M9KB_ADDR_V2   0x6D
#define M9KB_PIN_INT   12     // w spoczynku LOW, krótki impuls HIGH przy naciśnięciu
#define M9KB_PIN_LED   46     // podświetlenie klawiatury
#define M9KB_BUS1_SDA  20     // druga magistrala (Wire1)
#define M9KB_BUS1_SCL  21

#define M9KB_REG_HW    0x00   // wersja sprzętu kontrolera
#define M9KB_REG_FW    0xFE   // wersja programu kontrolera
#define M9KB_REG_KEY   0x05   // kod klawisza (kontroler STC8H); odczyt zdejmuje klawisz
#define M9KB_REG_KEY_EARLY 0x01   // wczesny kontroler (hw 0x03 / fw 0x10)

// kody klawiszy specjalnych (litery i cyfry to zwykłe ASCII)
#define M9K_ENTER      0x0D
#define M9K_BKSP       0x08
#define M9K_LEFT       0xB4
#define M9K_UP         0xB5
#define M9K_DOWN       0xB6
#define M9K_RIGHT      0xB7
#define M9K_MSG        0x81
#define M9K_HOME       0x82
#define M9K_F3         0x83   // klawisz funkcyjny pod Home
#define M9K_F4         0x84   // klawisz funkcyjny pod Wstecz
#define M9K_MAP        0x85
#define M9K_BACK       0x86
#define M9K_GPS_LONG   0x87
#define M9K_CTRL       0x90
#define M9K_ENTER_LONG 0xA3

class M9Keyboard {
  TwoWire* _wire = NULL;
  uint8_t  _addr = 0;
  uint8_t  _bus = 0xFF;        // 0 = Wire (SDA 7 / SCL 6), 1 = Wire1 (SDA 20 / SCL 21)
  uint8_t  _key_reg = M9KB_REG_KEY;
  int16_t  _hw = -1, _fw = -1;
  unsigned long _next_poll = 0;

public:
  uint8_t found[2][8];
  uint8_t num_found[2] = { 0, 0 };

  void begin();
  void scan();                 // skan obu magistral + wybór klawiatury i rejestru klawisza
  bool isFound() const { return _wire != NULL; }
  uint8_t addr() const { return _addr; }
  uint8_t bus() const { return _bus; }
  uint8_t keyReg() const { return _key_reg; }
  int16_t hwVersion() const { return _hw; }
  int16_t fwVersion() const { return _fw; }

  int  readReg(uint8_t reg);   // -1 = błąd odczytu
  int  readRaw(uint8_t* buf, int n);   // odczyt bez wskazania rejestru; zwraca liczbę bajtów

  // diagnostyka: co dały różne sposoby odczytu przy ostatnim przerwaniu
  struct Diag { int16_t r05, r01, r02, raw0, raw1; };
  Diag diag[6];
  int diag_len = 0;
  uint32_t read_errors = 0;
  // następny klawisz (0 = brak). Czyta po przerwaniu, a dodatkowo co 100 ms na wypadek zgubionego impulsu.
  uint8_t readKey();

  bool intLevel() const { return digitalRead(M9KB_PIN_INT) == HIGH; }
  uint32_t irqCount() const;

  void setBacklight(bool on) { digitalWrite(M9KB_PIN_LED, on ? HIGH : LOW); }
};
