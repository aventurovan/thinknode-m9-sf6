#pragma once
// Sterownik klawiatury ThinkNode M9 (osobny procesor STC8H na I2C) — własna implementacja (MIT).
//
// Piny 20/21 magistrali klawiatury są też pinami wbudowanego USB procesora; M9 ma osobny układ USB-UART,
// więc wbudowane USB odłączamy od tych pinów.
// Odczyt klawisza robi osobne zadanie (FreeRTOS) zaraz po przerwaniu, bo kontroler może trzymać kod krótko.
// WERSJA DIAGNOSTYCZNA: po każdym przerwaniu zapisuje rejestry 0x00–0x07 w trzech chwilach (od razu, po 20 ms, po 100 ms).

#include <Arduino.h>
#include <Wire.h>

#define M9KB_ADDR_V1   0x6C
#define M9KB_ADDR_V2   0x6D
#define M9KB_PIN_INT   12
#define M9KB_PIN_LED   46
#define M9KB_BUS1_SDA  20
#define M9KB_BUS1_SCL  21

// kody klawiszy specjalnych (litery i cyfry to zwykłe ASCII)
#define M9K_ENTER      0x0D
#define M9K_BKSP       0x08
#define M9K_LEFT       0xB4
#define M9K_UP         0xB5
#define M9K_DOWN       0xB6
#define M9K_RIGHT      0xB7
#define M9K_MSG        0x81
#define M9K_HOME       0x82
#define M9K_F3         0x83
#define M9K_F4         0x84
#define M9K_MAP        0x85
#define M9K_BACK       0x86
#define M9K_GPS_LONG   0x87
#define M9K_CTRL       0x90
#define M9K_ENTER_LONG 0xA3

#define M9KB_SNAP_REGS 8
#define M9KB_SNAPS     3

class M9Keyboard {
public:
  // diagnostyka: rejestry 0x00–0x07 po przerwaniu w trzech chwilach (-1 = błąd odczytu)
  struct Press { int16_t r[M9KB_SNAPS][M9KB_SNAP_REGS]; uint8_t key; };

  void begin();
  bool isFound() const { return _addr != 0; }
  uint8_t addr() const { return _addr; }
  uint8_t bus() const { return _bus; }
  int16_t hwVersion() const { return _hw; }
  int16_t fwVersion() const { return _fw; }
  uint32_t irqCount() const;
  uint32_t errors() const { return _errors; }

  uint8_t readKey();                       // następny klawisz z kolejki (0 = brak)
  int pressCount() const { return _press_len; }
  void getPress(int i, Press* out);        // 0 = najnowsze

  bool intLevel() const { return digitalRead(M9KB_PIN_INT) == HIGH; }
  void setBacklight(bool on) { digitalWrite(M9KB_PIN_LED, on ? HIGH : LOW); }

  // używane przez zadanie odczytu
  void taskLoop();

private:
  TwoWire* _wire = NULL;
  volatile uint8_t _addr = 0;
  uint8_t _bus = 0xFF;
  int16_t _hw = -1, _fw = -1;
  volatile uint32_t _errors = 0;

  uint8_t _queue[16];
  volatile uint8_t _q_head = 0, _q_tail = 0;
  Press _press[4];
  volatile int _press_len = 0;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

  bool probe(TwoWire* w, uint8_t a);
  void find();
  int readReg(uint8_t reg);
  void snapshot(int16_t* dest);
  void push(uint8_t k);
};
