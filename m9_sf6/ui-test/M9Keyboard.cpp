#include "M9Keyboard.h"
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>

static volatile uint32_t kb_irq_count = 0;
static TaskHandle_t kb_task = NULL;
static M9Keyboard* kb_self = NULL;

static void IRAM_ATTR kbIsr() {
  kb_irq_count++;
  BaseType_t woken = pdFALSE;
  if (kb_task) vTaskNotifyGiveFromISR(kb_task, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void kbTask(void*) {
  for (;;) kb_self->taskLoop();
}

bool M9Keyboard::probe(TwoWire* w, uint8_t a) {
  w->beginTransmission(a);
  return w->endTransmission() == 0;
}

// szukamy tylko pod dwoma adresami klawiatury (pełny skan magistrali wydłużał start)
void M9Keyboard::find() {
  TwoWire* buses[2] = { &Wire1, &Wire };
  const uint8_t addrs[2] = { M9KB_ADDR_V2, M9KB_ADDR_V1 };
  for (int b = 0; b < 2; b++)
    for (int i = 0; i < 2; i++)
      if (probe(buses[b], addrs[i])) {
        _wire = buses[b]; _bus = b == 0 ? 1 : 0; _addr = addrs[i];
        _hw = readReg(0x00);
        _fw = readReg(0xFE);
        return;
      }
}

void M9Keyboard::begin() {
  pinMode(M9KB_PIN_INT, INPUT);
  pinMode(M9KB_PIN_LED, OUTPUT);
  setBacklight(true);

  REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  Wire1.begin(M9KB_BUS1_SDA, M9KB_BUS1_SCL, 100000);
  Wire1.setTimeOut(20);
  find();

  kb_self = this;
  attachInterrupt(digitalPinToInterrupt(M9KB_PIN_INT), kbIsr, RISING);
  xTaskCreatePinnedToCore(kbTask, "m9kb", 4096, NULL, 3, &kb_task, 1);
}

int M9Keyboard::readReg(uint8_t reg) {
  if (_wire == NULL) return -1;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) { _errors++; return -1; }
  if (_wire->requestFrom((int)_addr, 1) != 1) { _errors++; return -1; }
  return _wire->read();
}

void M9Keyboard::snapshot(int16_t* dest) {
  for (int i = 0; i < M9KB_SNAP_REGS; i++) dest[i] = readReg(i);
}

void M9Keyboard::push(uint8_t k) {
  uint8_t next = (_q_tail + 1) & 15;
  if (next != _q_head) { _queue[_q_tail] = k; _q_tail = next; }
}

static bool isKey(int k) { return k > 0 && k != 0xFF; }

// Kontroler odpowiada z opóźnieniem o jeden odczyt (bajt do wysłania przygotowuje przed przyjęciem numeru rejestru),
// dlatego ten sam rejestr czytamy dwa razy pod rząd, a zrzut 0x00–0x07 robimy po kolei.
void M9Keyboard::taskLoop() {
  if (_addr == 0) {                               // klawiatury nie ma: szukamy co 2 s
    vTaskDelay(pdMS_TO_TICKS(2000));
    find();
    return;
  }
  // czekamy na przerwanie; dodatkowo co 20 ms sprawdzamy, czy linia INT nie wisi w stanie wysokim
  // (zgłoszenie, którego nikt nie odebrał, blokuje kolejne zbocza)
  bool irq = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)) != 0;
  if (!irq && digitalRead(M9KB_PIN_INT) == LOW) return;

  Press p;
  int a = readReg(0x01);
  int b = readReg(0x01);
  int c = readReg(0x02);
  int d = readReg(0x02);
  snapshot(p.r[0]);                               // 0x00–0x07 po kolei
  p.r[1][0] = a; p.r[1][1] = b; p.r[1][2] = c; p.r[1][3] = d;
  p.r[1][4] = digitalRead(M9KB_PIN_INT); p.r[1][5] = irq; p.r[1][6] = -1; p.r[1][7] = -1;

  int k = 0;
  const int cand[4] = { b, d, a, c };
  for (int i = 0; i < 4 && !k; i++) if (isKey(cand[i]) && cand[i] != 0x03 && cand[i] != 0x7F) k = cand[i];
  p.key = (uint8_t)k;

  // czekamy, aż linia INT opadnie (maks. 300 ms), żeby jedno naciśnięcie nie dało kilku klawiszy
  for (int t = 0; t < 30 && digitalRead(M9KB_PIN_INT) == HIGH; t++) vTaskDelay(pdMS_TO_TICKS(10));
  snapshot(p.r[2]);
  ulTaskNotifyTake(pdTRUE, 0);

  portENTER_CRITICAL(&_mux);
  if (_press_len == 4) { memmove(&_press[0], &_press[1], sizeof(Press) * 3); _press_len = 3; }
  _press[_press_len] = p;
  _press_len = _press_len + 1;
  if (k) push((uint8_t)k);
  portEXIT_CRITICAL(&_mux);
}

uint8_t M9Keyboard::readKey() {
  if (_q_head == _q_tail) return 0;
  uint8_t k = _queue[_q_head];
  _q_head = (_q_head + 1) & 15;
  return k;
}

void M9Keyboard::getPress(int i, Press* out) {
  portENTER_CRITICAL(&_mux);
  *out = _press[_press_len - 1 - i];
  portEXIT_CRITICAL(&_mux);
}

uint32_t M9Keyboard::irqCount() const { return kb_irq_count; }
