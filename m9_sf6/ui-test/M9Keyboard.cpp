#include "M9Keyboard.h"
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>

static volatile uint32_t kb_irq_count = 0;
static volatile bool kb_irq_flag = false;

static void IRAM_ATTR kbIsr() {
  kb_irq_count++;
  kb_irq_flag = true;
}

void M9Keyboard::begin() {
  pinMode(M9KB_PIN_INT, INPUT);
  pinMode(M9KB_PIN_LED, OUTPUT);
  setBacklight(true);

  // odłączamy wbudowane USB od pinów 20/21 (M9 ma osobny układ USB-UART), żeby I2C klawiatury działało poprawnie
  REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  Wire1.begin(M9KB_BUS1_SDA, M9KB_BUS1_SCL, 100000);

  scan();
  attachInterrupt(digitalPinToInterrupt(M9KB_PIN_INT), kbIsr, RISING);
}

void M9Keyboard::scan() {
  TwoWire* buses[2] = { &Wire, &Wire1 };
  _wire = NULL; _addr = 0; _bus = 0xFF;
  for (uint8_t b = 0; b < 2; b++) {
    num_found[b] = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
      buses[b]->beginTransmission(a);
      if (buses[b]->endTransmission() == 0) {
        if (num_found[b] < 8) found[b][num_found[b]++] = a;
        if ((a == M9KB_ADDR_V1 || a == M9KB_ADDR_V2) && _wire == NULL) {
          _wire = buses[b]; _addr = a; _bus = b;
        }
      }
    }
  }
  if (_wire) {
    _hw = readReg(M9KB_REG_HW);
    _fw = readReg(M9KB_REG_FW);
    _key_reg = (_hw == 0x03 && _fw == 0x10) ? M9KB_REG_KEY_EARLY : M9KB_REG_KEY;
    for (int i = 0; i < 4 && readKey() != 0; i++) { }    // zdejmij klawisz zapamiętany przed startem
  }
}

int M9Keyboard::readReg(uint8_t reg) {
  if (_wire == NULL) return -1;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return -1;
  if (_wire->requestFrom((int)_addr, 1) != 1) return -1;
  return _wire->read();
}

int M9Keyboard::readRaw(uint8_t* buf, int n) {
  if (_wire == NULL) return 0;
  int got = _wire->requestFrom((int)_addr, n);
  for (int i = 0; i < got; i++) buf[i] = _wire->read();
  return got;
}

static bool isKey(int k) { return k > 0 && k != 0xFF; }

uint8_t M9Keyboard::readKey() {
  if (_wire == NULL) return 0;
  unsigned long now = millis();
  bool irq = kb_irq_flag;
  if (!irq && (long)(now - _next_poll) < 0) return 0;
  kb_irq_flag = false;
  _next_poll = now + 100;

  if (!irq) {                          // odpytywanie bez przerwania: tylko rejestr, który już raz zadziałał
    int k = readReg(_key_reg);
    if (k < 0) { read_errors++; return 0; }
    return isKey(k) ? (uint8_t)k : 0;
  }

  // po przerwaniu (wersja diagnostyczna): rejestr 0x05, potem 0x01, 0x02 i odczyt bez rejestru;
  // pierwszy, który da kod, zostaje rejestrem klawisza
  Diag d;
  d.r05 = readReg(0x05);
  d.r01 = readReg(0x01);
  d.r02 = readReg(0x02);
  uint8_t raw[2] = { 0, 0 };
  int n = readRaw(raw, 2);
  d.raw0 = n > 0 ? raw[0] : -1;
  d.raw1 = n > 1 ? raw[1] : -1;
  if (d.r05 < 0 || d.r01 < 0 || d.r02 < 0) read_errors++;
  if (diag_len == 6) { memmove(&diag[0], &diag[1], sizeof(Diag) * 5); diag_len--; }
  diag[diag_len++] = d;

  int k = 0;
  if (isKey(d.r05)) { k = d.r05; _key_reg = 0x05; }
  else if (isKey(d.r01) && d.r01 != 0x03) { k = d.r01; _key_reg = 0x01; }
  else if (isKey(d.r02)) { k = d.r02; _key_reg = 0x02; }
  else if (isKey(d.raw0)) k = d.raw0;
  return (uint8_t)k;
}

uint32_t M9Keyboard::irqCount() const { return kb_irq_count; }
