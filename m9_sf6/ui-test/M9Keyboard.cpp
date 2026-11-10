#include "M9Keyboard.h"

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

  // Wire (magistrala peryferiów) uruchamia płytka w board.begin(); drugą uruchamiamy tutaj
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
}

int M9Keyboard::readReg(uint8_t reg) {
  if (_wire == NULL) return -1;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return -1;
  if (_wire->requestFrom((int)_addr, 1) != 1) return -1;
  return _wire->read();
}

void M9Keyboard::readRegs(int16_t* dest, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) dest[i] = (int16_t)readReg(i);
}

uint32_t M9Keyboard::irqCount() const { return kb_irq_count; }

bool M9Keyboard::takeIrq() {
  if (!kb_irq_flag) return false;
  kb_irq_flag = false;
  return true;
}
